#include "unskip.h"
#include "../resource.h"
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
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
    std::string fn = std::string("locale/") + locale + ".txt";
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

// Helper to run a process and capture output
static std::pair<int,std::string> RunProcessCaptureExitCodeLocal(const std::wstring &cmd, int timeoutMs = 5000) {
    std::pair<int,std::string> res = {-1, std::string()};
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;
    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return res;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = hWrite; si.hStdError = hWrite; si.hStdInput = NULL;
    PROCESS_INFORMATION pi{};
    std::wstring cmdCopy = cmd;
    if (!CreateProcessW(NULL, &cmdCopy[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(hWrite); CloseHandle(hRead); return res; }
    CloseHandle(hWrite);
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs > 0 ? (DWORD)timeoutMs : INFINITE);
    if (wait == WAIT_TIMEOUT) { TerminateProcess(pi.hProcess, 1); res.first = -2; }
    else { DWORD exitCode = 0; GetExitCodeProcess(pi.hProcess, &exitCode); res.first = (int)exitCode; }
    std::string out; const DWORD bufSize = 4096; char buf[bufSize]; DWORD read = 0;
    while (ReadFile(hRead, buf, bufSize, &read, NULL) && read > 0) out.append(buf, buf + read);
    CloseHandle(hRead); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    res.second = out; return res;
}


// Modal dialog implemented with a simple window and listbox

// Entry type used by the dialog
struct UnskipEntry { std::string id; std::string name; std::string ver; };

// Context stored for each dialog instance
struct DialogContext {
    HWND parent;
    HWND hList;
    std::vector<UnskipEntry> entries;
    bool didAny = false;
};

// Window proc for the unskip dialog. Handles button clicks directly so
// WM_COMMAND sent via SendMessage is received here and the Cancel button works.
static LRESULT CALLBACK UnskipWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogContext *ctx = (DialogContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        if (!ctx) return DefWindowProcW(hWnd, msg, wParam, lParam);
        if (id == 1002) { // Cancel
            // Bring main window to front and close the dialog
            if (ctx && ctx->parent && IsWindow(ctx->parent)) PostMessageW(ctx->parent, (WM_APP+7), 0, 0);
            DestroyWindow(hWnd);
            return 0;
        }
        if (id == 1001) { // Unskip
            int sel = (int)SendMessageW(ctx->hList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)ctx->entries.size()) {
                MessageBoxA(hWnd, "Select an entry first.", "WinUpdate", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            try {
                AppendLog(std::string("Unskip: attempting to remove id=") + ctx->entries[sel].id + " name='" + ctx->entries[sel].name + "'\n");
                bool ok = false;
                try {
                    auto m = LoadSkippedMap();
                    if (m.empty()) {
                        AppendLog("Unskip: LoadSkippedMap returned empty map\n");
                    } else {
                        // sanitize helper (keep same logic as skip_update::IsSkipped)
                        auto sanitize = [](const std::string &s)->std::string {
                            std::string out; out.reserve(s.size());
                            for (unsigned char c : s) if (!isspace(c) && c >= 32) out.push_back((char)c);
                            return out;
                        };
                        std::string targetId = sanitize(ctx->entries[sel].id);
                        std::string targetName = sanitize(ctx->entries[sel].name);
                        std::string foundKey;
                        for (auto &kv : m) {
                            std::string key_s = sanitize(kv.first);
                            // exact match by id or by display name
                            if (key_s == targetId || key_s == targetName) { foundKey = kv.first; break; }
                            // case-insensitive compare
                            auto toLower = [](std::string s){ for (auto &c : s) c = (char)tolower((unsigned char)c); return s; };
                            if (toLower(key_s) == toLower(targetId) || toLower(key_s) == toLower(targetName)) { foundKey = kv.first; break; }
                            // strip trailing version-like tokens from key and compare
                            auto stripTrailingVersionTokens = [](std::string s){ auto trim_inplace = [](std::string &x){ size_t a = x.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { x.clear(); return; } size_t b = x.find_last_not_of(" \t\r\n"); x = x.substr(a, b-a+1); }; trim_inplace(s); auto isVersionToken = [](const std::string &t){ if (t.empty()) return false; for (char c : t) { if (!(isdigit((unsigned char)c) || c=='.' || c=='-' || c=='_')) return false; } return true; }; while (true) { size_t p = s.find_last_of(" \t"); if (p==std::string::npos) break; std::string last = s.substr(p+1); if (isVersionToken(last)) { s = s.substr(0, p); trim_inplace(s); continue; } break; } return s; };
                            std::string key_stripped = stripTrailingVersionTokens(kv.first);
                            std::string ks_s = sanitize(key_stripped);
                            if (ks_s == targetId || ks_s == targetName) { foundKey = kv.first; break; }
                        }
                        if (!foundKey.empty()) {
                            AppendLog(std::string("Unskip: resolved entry to remove key='") + foundKey + "\n");
                            m.erase(foundKey);
                            ok = SaveSkippedMap(m);
                            AppendLog(std::string("Unskip: SaveSkippedMap returned ") + (ok?"true":"false") + "\n");
                        } else {
                            AppendLog(std::string("Unskip: could not resolve key for id='") + ctx->entries[sel].id + " name='" + ctx->entries[sel].name + "'\n");
                        }
                    }
                } catch(...) { ok = false; }
                AppendLog(std::string("Unskip: final removal result = ") + (ok?"true":"false") + "\n");
                if (ok) {
                    // trigger a refresh on the main window so UI reflects change
                    if (ctx->parent && IsWindow(ctx->parent)) {
                        PostMessageW(ctx->parent, (WM_APP + 1), 1, 0);
                        // also request the main window be brought forward
                        PostMessageW(ctx->parent, (WM_APP + 7), 0, 0);
                    }
                    ctx->didAny = true;
                    DestroyWindow(hWnd);
                    return 0;
                } else {
                    MessageBoxA(hWnd, "Failed to remove skip entry.", "WinUpdate", MB_OK | MB_ICONERROR);
                }
            } catch(...) { MessageBoxA(hWnd, "Failed to remove skip entry.", "WinUpdate", MB_OK | MB_ICONERROR); }
            return 0;
        }
    }
    if (msg == WM_DESTROY) {
        // leave context alive until ShowUnskipDialog cleans it up
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// --- custom tooltip implementation for Unskip dialog (file-scope) ---
static bool g_unskip_tip_class_registered = false;
static const wchar_t *kUnskipTipClass = L"WUP_UnskipCustomTip";
static const int kUnskipTipPadX = 30;
static const int kUnskipTipPadY = 12;

static void EnsureUnskipTipClassRegistered() {
    if (g_unskip_tip_class_registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_NOCLOSE;
    wc.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)->LRESULT {
        switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH hbr = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
            FillRect(hdc, &rc, hbr);
            DeleteObject(hbr);
            FrameRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOWFRAME));
            int len = GetWindowTextLengthW(hwnd);
            std::vector<wchar_t> buf(len + 1);
            if (len > 0) GetWindowTextW(hwnd, buf.data(), len + 1);
            else buf[0] = 0;
            SetTextColor(hdc, GetSysColor(COLOR_INFOTEXT));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
            HGDIOBJ oldf = NULL;
            if (hf) oldf = SelectObject(hdc, hf);
            RECT inner = rc; inner.left += kUnskipTipPadX; inner.right -= kUnskipTipPadX; inner.top += kUnskipTipPadY; inner.bottom -= kUnskipTipPadY;
            DrawTextW(hdc, buf.data(), -1, &inner, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
            if (oldf) SelectObject(hdc, oldf);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    };
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
    wc.lpszClassName = kUnskipTipClass;
    RegisterClassExW(&wc);
    g_unskip_tip_class_registered = true;
}

static std::unordered_map<HWND, HWND> g_unskip_tooltips;
static std::unordered_map<HWND, std::wstring> g_unskip_texts;

static HWND EnsureUnskipTooltipForList(HWND hList) {
    auto it = g_unskip_tooltips.find(hList);
    if (it != g_unskip_tooltips.end() && IsWindow(it->second)) return it->second;
    EnsureUnskipTipClassRegistered();
    HWND owner = GetAncestor(hList, GA_ROOT);
    if (!owner) owner = GetParent(hList);
    HWND tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kUnskipTipClass, L"", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!tip) return NULL;
    SetWindowPos(tip, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    g_unskip_tooltips[hList] = tip;
    g_unskip_texts[hList] = L"";
    HFONT lf = (HFONT)SendMessageW(hList, WM_GETFONT, 0, 0);
    if (lf) SendMessageW(tip, WM_SETFONT, (WPARAM)lf, TRUE);
    return tip;
}

static LRESULT CALLBACK Unskip_ListSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    static std::unordered_map<HWND, int> lastIndex;
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
        // ensure we get WM_MOUSELEAVE and WM_MOUSEHOVER
        TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE | TME_HOVER; tme.hwndTrack = hwnd; TrackMouseEvent(&tme);
        // try fast compute using item height and top index
        int idx = -1;
        LRESULT count = SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
        LRESULT itemH = SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0);
        LRESULT top = SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
        if (itemH > 0) {
            int rel = pt.y;
            int calc = (int)top + (rel / (int)itemH);
            if (calc >= 0 && calc < (int)count) idx = calc;
        }
        if (idx == -1) {
            LPARAM lp = MAKELPARAM(pt.x, pt.y);
            LRESULT res = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp);
            idx = LOWORD(res);
        }
            if (idx < 0) {
            auto it = g_unskip_tooltips.find(hwnd);
            if (it != g_unskip_tooltips.end() && IsWindow(it->second)) ShowWindow(it->second, SW_HIDE);
            lastIndex[hwnd] = -1;
            break;
        }
        if (lastIndex[hwnd] == idx) break;
        lastIndex[hwnd] = idx;
        HWND tip = EnsureUnskipTooltipForList(hwnd);
        if (!tip) break;
        std::wstring text = g_unskip_texts[hwnd];
        if (text.empty()) break;
        SetWindowTextW(tip, text.c_str());
        const int padX = kUnskipTipPadX, padY = kUnskipTipPadY;
        HDC hdc = GetDC(tip);
        HFONT hf = (HFONT)SendMessageW(tip, WM_GETFONT, 0, 0);
        HGDIOBJ oldf = NULL; if (hf) oldf = SelectObject(hdc, hf);
        RECT rcCalc = {0,0,0,0}; DrawTextW(hdc, text.c_str(), -1, &rcCalc, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        int naturalW = rcCalc.right - rcCalc.left; int naturalH = rcCalc.bottom - rcCalc.top;
        if (oldf) SelectObject(hdc, oldf);
        ReleaseDC(tip, hdc);
        int w = naturalW + (padX * 2); int h = naturalH + (padY * 2);
        RECT itemRect; SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRect);
        POINT tl = { itemRect.left, itemRect.top }; POINT br = { itemRect.right, itemRect.bottom };
        ClientToScreen(hwnd, &tl); ClientToScreen(hwnd, &br);
        int tipX = (tl.x + br.x) / 2 - w/2; int tipY = tl.y - h - 8;
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        if (tipX < wa.left) tipX = wa.left + 4; if (tipX + w > wa.right) tipX = wa.right - w - 4;
        if (tipY < wa.top) tipY = br.y + 8;
        SetWindowPos(tip, HWND_TOP, tipX, tipY, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(tip, NULL, TRUE);
        break;
    }
    case WM_MOUSEHOVER: {
        // Treat hover like mouse move so the tooltip appears when the mouse lingers
        SendMessageW(hwnd, WM_MOUSEMOVE, wParam, lParam);
        break;
    }
    case WM_MOUSELEAVE: {
        auto it = g_unskip_tooltips.find(hwnd);
        if (it != g_unskip_tooltips.end() && IsWindow(it->second)) ShowWindow(it->second, SW_HIDE);
        lastIndex[hwnd] = -1;
        break;
    }
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED: {
        auto it = g_unskip_tooltips.find(hwnd);
        if (it != g_unskip_tooltips.end() && IsWindow(it->second)) ShowWindow(it->second, SW_HIDE);
        lastIndex[hwnd] = -1;
        break;
    }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

bool ShowUnskipDialog(HWND parent) {
    std::string locale = "en_GB";
    // Load locale from APPDATA wup_settings.ini
    char buf[MAX_PATH]; DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    std::string settingsIni;
    if (len > 0 && len < MAX_PATH) settingsIni = std::string(buf) + "\\WinUpdate\\wup_settings.ini";
    std::ifstream ifs(settingsIni, std::ios::binary);
    if (ifs) {
        std::string line; bool inLang = false;
        auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
        while (std::getline(ifs, line)) {
            trim(line); if (line.empty()) continue; if (line.front() == '[') { inLang = (line=="[language]"); continue; }
            if (inLang) { locale = line; break; } // Return full locale code (en_GB, nb_NO, sv_SE)
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
    
    // Build a map from the in-memory winget output to get ALL package names (including skipped)
    std::map<std::string, std::string> idToName; // id -> display name
    try {
        std::string raw;
        {
            std::lock_guard<std::mutex> lk(g_last_winget_raw_mutex);
            raw = g_last_winget_raw;
        }
        if (!raw.empty()) {
            AppendLog("[unskip] Parsing in-memory winget output for display names\n");
            std::istringstream iss(raw);
            std::string line;
            bool pastHeader = false;
            while (std::getline(iss, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                if (!pastHeader) {
                    if (line.find("----") != std::string::npos) { pastHeader = true; }
                    continue;
                }
                if (line.find("upgrades available") != std::string::npos) break;
                if (line.empty()) continue;
                
                // Split into tokens
                std::istringstream ls(line);
                std::vector<std::string> tokens;
                std::string tok;
                while (ls >> tok) tokens.push_back(tok);
                
                if (tokens.size() < 5) continue;
                
                size_t n = tokens.size();
                // Right to left: Source, Available, Version, Id, then Name is everything else
                std::string id = tokens[n-4];
                std::string name;
                for (size_t i = 0; i + 4 < n; ++i) {
                    if (i > 0) name += " ";
                    name += tokens[i];
                }
                if (!name.empty() && !id.empty()) {
                    idToName[id] = name;
                    AppendLog(std::string("[unskip] In-memory: id='") + id + "' name='" + name + "'\n");
                }
            }
        }
    } catch(...) {}
    
    // Get display names from memory first, then in-memory winget output, then g_packages, then prettify
    for (auto &kv : skipped) {
        Entry e;
        // Trim the key (id might have trailing spaces from INI parsing)
        e.id = kv.first;
        auto trim = [](std::string s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) return std::string(); size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1); };
        e.id = trim(e.id);
        e.ver = kv.second;
        // Try memory first
        e.name = GetDisplayNameForId(e.id);
        // If still same as id, try in-memory winget output
        if (e.name == e.id) {
            auto it = idToName.find(e.id);
            if (it != idToName.end()) {
                e.name = it->second;
                AppendLog(std::string("[unskip] Using in-memory name='") + e.name + "' for id='" + e.id + "'\n");
            }
        }
        // If still same as id, try g_packages
        if (e.name == e.id) {
            try {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                for (auto &p : g_packages) {
                    if (p.first == e.id) { e.name = p.second; break; }
                }
            } catch(...) {}
        }
        entries.push_back(e);
    }

    // Helper to prettify an id into a readable name if no display name found
    auto PrettyNameFromId = [](const std::string &id)->std::string{
        if (id.empty()) return id;
        // if id contains dots, take last token
        size_t p = id.find_last_of('.');
        std::string base = (p==std::string::npos) ? id : id.substr(p+1);
        // split on camelCase or capitals
        std::string out;
        for (size_t i = 0; i < base.size(); ++i) {
            char c = base[i];
            if (i>0 && isupper((unsigned char)c) && (islower((unsigned char)base[i-1]) || (i+1<base.size() && islower((unsigned char)base[i+1])))) out.push_back(' ');
            if (c == '_' || c == '-') out.push_back(' ');
            else out.push_back(c);
        }
        // capitalize first letter
        if (!out.empty()) out[0] = (char)toupper((unsigned char)out[0]);
        return out;
    };

    // If still unknown display names, prettify from id
    for (auto &e : entries) {
        if (e.name == e.id) e.name = PrettyNameFromId(e.id);
    }

    // create simple modal dialog
    static const wchar_t *kClass = L"WUP_UnskipDlg";
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = UnskipWndProc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = kClass; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        RegisterClassExW(&wc); reg = true;
    }
    int W = 520, H = 360;
    RECT prc{}; int px=100, py=100;
    if (parent && IsWindow(parent)) { GetWindowRect(parent, &prc); px = prc.left + 40; py = prc.top + 40; }
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, Utf8ToWide(LoadI18nValue(locale, "unskip_dialog_title")).c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, px, py, W, H, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!hDlg) return false;
    
    // Set app icon
    HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (hIcon) {
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    // allocate context for this dialog and copy entries
    DialogContext *ctx = new DialogContext();
    ctx->parent = parent;
    ctx->entries.reserve(entries.size());
    for (auto &e : entries) ctx->entries.push_back({ e.id, e.name, e.ver });
    // create listbox
    // adjust dialog height to fit entries (but clamp to reasonable sizes)
    int itemH = 20;
    int listH = std::max(80, std::min((int)entries.size() * (itemH + 2) + 8, 360));
    int dlgH = 40 + listH + 80; // top padding + list + buttons area
    if (dlgH > 600) dlgH = 600;
    H = dlgH;
    // Resize the dialog window so it matches the calculated height
    if (hDlg && IsWindow(hDlg)) {
        SetWindowPos(hDlg, NULL, 0, 0, W, H, SWP_NOMOVE | SWP_NOZORDER);
    }
    HWND hList = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 12, 12, W-24, listH, hDlg, NULL, GetModuleHandleW(NULL), NULL);
    // fill listbox and keep mapping: show package display name first, then version (no id)
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string line = entries[i].name + "  -  " + entries[i].ver;
        std::wstring wl;
        int needed = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, NULL, 0);
        if (needed>0) { std::vector<wchar_t> buf(needed); MultiByteToWideChar(CP_UTF8,0,line.c_str(),-1,buf.data(),needed); wl = buf.data(); }
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)wl.c_str());
    }
    // store list HWND in context and associate context with dialog
    ctx->hList = hList;
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);
    // buttons: Unskip and Cancel
    std::string txtUn = LoadI18nValue(locale, "unskip"); if (txtUn.empty()) txtUn = "Unskip";
    std::string txtCancel = LoadI18nValue(locale, "btn_cancel"); if (txtCancel.empty()) txtCancel = "Cancel";
    std::wstring wun = Utf8ToWide(txtUn);
    std::wstring wcancel = Utf8ToWide(txtCancel);
    HWND hBtnUn = CreateWindowExW(0, L"BUTTON", wun.c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, W-220, H-88, 100, 30, hDlg, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", wcancel.c_str(), WS_CHILD | WS_VISIBLE, W-108, H-88, 96, 30, hDlg, (HMENU)1002, GetModuleHandleW(NULL), NULL);

    // ensure common controls used by subclassing
    INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icce);
    // set the tooltip text into the map for this list
    std::string tipText = LoadI18nValue(locale, "unskip_tooltip"); if (tipText.empty()) tipText = "Click the row and press Unskip to execute the unskip";
    std::wstring wtip = Utf8ToWide(tipText);
    HWND preTip = EnsureUnskipTooltipForList(hList);
    g_unskip_texts[hList] = wtip;
    // subclass the list to show the custom tooltip on hover
    SetWindowSubclass(hList, Unskip_ListSubclassProc, 0xBEEFDEAD, 0);

    ShowWindow(hDlg, SW_SHOW);
    // modal message loop for this dialog
    bool didAny = false;
    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        // let dialog manager process dialog-specific keys first
        if (IsDialogMessage(hDlg, &msg)) continue;
        // WM_COMMANDs are handled in the dialog window procedure (UnskipWndProc)
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    // retrieve didAny from context and cleanup
    if (ctx) {
        didAny = ctx->didAny;
        delete ctx;
        ctx = nullptr;
    }
    return didAny;
}
