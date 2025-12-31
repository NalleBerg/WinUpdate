#include "unskip.h"
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include "skip_update.h"
#include "logging.h"
#include "parsing.h"

// Simple i18n loader (reads i18n/<locale>.txt like other code)
static std::string ReadFileToString(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream ss; ss << ifs.rdbuf();
    return ss.str();
}

static std::string LoadI18nValue(const std::string &locale, const std::string &key) {
    std::string fn = std::string("i18n/") + locale + ".txt";
    std::string txt = ReadFileToString(fn);
    if (txt.empty()) return std::string();
    std::istringstream iss(txt);
    std::string ln;
    while (std::getline(iss, ln)) {
        if (ln.empty()) continue;
        if (ln[0] == '#' || ln[0] == ';') continue;
        size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        std::string k = ln.substr(0, eq);
        std::string v = ln.substr(eq+1);
        if (k == key) return v;
    }
    return std::string();
}

// Helper to convert UTF-8 to wide
static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

// Modal dialog implemented with a simple window and listbox
bool ShowUnskipDialog(HWND parent) {
    std::string locale = "en";
    // try to read locale from settings file similar to other code
    char buf[MAX_PATH]; DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    std::string settingsIni;
    if (len > 0 && len < MAX_PATH) settingsIni = std::string(buf) + "\\WinUpdate\\wup_settings.ini";
    // attempt to read `[language]` section
    std::ifstream ifs(settingsIni, std::ios::binary);
    if (ifs) {
        std::string line; bool inLang = false;
        auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
        while (std::getline(ifs, line)) {
            trim(line); if (line.empty()) continue; if (line.front() == '[') { inLang = (line=="[language]"); continue; }
            if (inLang) { locale = line; break; }
        }
    }

    auto skipped = LoadSkippedMap();
    if (skipped.empty()) {
        std::string msg = LoadI18nValue(locale, "no_skipped"); if (msg.empty()) msg = "No skipped entries.";
        MessageBoxA(parent, msg.c_str(), "WinUpdate", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    // Prepare list of entries (id, display, version)
    struct Entry { std::string id; std::string name; std::string ver; };
    std::vector<Entry> entries;
    // try to resolve display names from g_packages (extern provided by parsing.h)
    for (auto &kv : skipped) {
        Entry e; e.id = kv.first; e.ver = kv.second; e.name = kv.first;
        try {
            std::lock_guard<std::mutex> lk(g_packages_mutex);
            for (auto &p : g_packages) {
                if (p.first == kv.first) { e.name = p.second; break; }
            }
        } catch(...) {}
        entries.push_back(e);
    }

    // If any entry still shows id as name, attempt a fresh winget probe to populate g_packages
    bool needProbe = false;
    for (auto &e : entries) if (e.name == e.id) { needProbe = true; break; }
    if (needProbe) {
        try {
            // Re-check g_packages via parsing of most recent raw if available
            std::string raw = ReadMostRecentRawWinget();
            if (!raw.empty()) ParseWingetTextForPackages(raw);
            // update names where possible
            for (auto &e : entries) {
                try { std::lock_guard<std::mutex> lk(g_packages_mutex); for (auto &p : g_packages) if (p.first == e.id) { e.name = p.second; break; } } catch(...) {}
            }
        } catch(...) {}
    }

    // create simple modal dialog
    static const wchar_t *kClass = L"WUP_UnskipDlg";
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = kClass; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        RegisterClassExW(&wc); reg = true;
    }
    int W = 520, H = 360;
    RECT prc{}; int px=100, py=100;
    if (parent && IsWindow(parent)) { GetWindowRect(parent, &prc); px = prc.left + 40; py = prc.top + 40; }
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, Utf8ToWide(LoadI18nValue(locale, "unskip_dialog_title")).c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, px, py, W, H, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!hDlg) return false;
    // create listbox
    HWND hList = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | LBS_STANDARD | LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 12, 12, W-24, H-120, hDlg, NULL, GetModuleHandleW(NULL), NULL);
    // fill listbox and keep mapping
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string line = entries[i].name + "  -  " + entries[i].ver + "  [" + entries[i].id + "]";
        std::wstring wl;
        int needed = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, NULL, 0);
        if (needed>0) { std::vector<wchar_t> buf(needed); MultiByteToWideChar(CP_UTF8,0,line.c_str(),-1,buf.data(),needed); wl = buf.data(); }
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)wl.c_str());
    }
    // buttons: Unskip and Cancel
    std::string txtUn = LoadI18nValue(locale, "unskip"); if (txtUn.empty()) txtUn = "Unskip";
    std::string txtCancel = LoadI18nValue(locale, "btn_cancel"); if (txtCancel.empty()) txtCancel = "Cancel";
    std::wstring wun = Utf8ToWide(txtUn);
    std::wstring wcancel = Utf8ToWide(txtCancel);
    HWND hBtnUn = CreateWindowExW(0, L"BUTTON", wun.c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, W-220, H-88, 100, 30, hDlg, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", wcancel.c_str(), WS_CHILD | WS_VISIBLE, W-108, H-88, 96, 30, hDlg, (HMENU)1002, GetModuleHandleW(NULL), NULL);

    ShowWindow(hDlg, SW_SHOW);
    // modal message loop for this dialog
    bool didAny = false;
    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_COMMAND && msg.hwnd == hDlg) {
            int id = LOWORD(msg.wParam);
            if (id == 1002) { DestroyWindow(hDlg); break; }
            if (id == 1001) {
                int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
                if (sel < 0 || sel >= (int)entries.size()) {
                    std::string selmsg = LoadI18nValue(locale, "select_entry"); if (selmsg.empty()) selmsg = "Select an entry first.";
                    MessageBoxA(hDlg, selmsg.c_str(), "WinUpdate", MB_OK | MB_ICONINFORMATION);
                } else {
                    // confirm
                    std::string q = LoadI18nValue(locale, "confirm_unskip"); if (q.empty()) q = "Yes, unskip it!";
                    std::wstring qw = Utf8ToWide(q);
                    int mb = MessageBoxW(hDlg, qw.c_str(), Utf8ToWide(LoadI18nValue(locale, "app_title")).c_str(), MB_YESNO | MB_ICONQUESTION);
                    if (mb == IDYES) {
                        // perform unskip
                        try {
                            bool ok = RemoveSkippedEntry(entries[sel].id);
                            AppendLog(std::string("Unskip: removed id=") + entries[sel].id + "\n");
                            if (ok) {
                                // request refresh of main UI
                                PostMessageW(parent, (WM_APP + 1), 1, 0);
                                // remove from listbox and vector
                                SendMessageW(hList, LB_DELETESTRING, sel, 0);
                                entries.erase(entries.begin() + sel);
                                didAny = true;
                            } else {
                                MessageBoxA(hDlg, "Failed to remove skip entry.", "WinUpdate", MB_OK | MB_ICONERROR);
                            }
                        } catch(...) { MessageBoxA(hDlg, "Failed to remove skip entry.", "WinUpdate", MB_OK | MB_ICONERROR); }
                    }
                }
            }
        }
        if (!IsWindow(hDlg)) break;
        if (!IsDialogMessage(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    }
    return didAny;
}
