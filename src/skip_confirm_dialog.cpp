#include "skip_confirm_dialog.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include "logging.h"

// Window proc for the fallback dialog to handle button clicks
static LRESULT CALLBACK SkipConfirmFallbackProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        // store result pointer in window property
        HANDLE h = GetPropW(hwnd, L"WUP_RES");
        int *p = h ? (int*)h : nullptr;
        AppendLog(std::string("[skip_confirm] fallback proc WM_COMMAND id=") + std::to_string(id) + " p=" + std::to_string((uintptr_t)p) + "\n");
        if (id == 1001) { if (p) *p = 1; DestroyWindow(hwnd); return 0; }
        if (id == 1002) { if (p) *p = 0; DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        // cleanup font property if set
        {
            HANDLE hf = GetPropW(hwnd, L"WUP_FONT");
            if (hf) {
                DeleteObject((HFONT)hf);
                RemovePropW(hwnd, L"WUP_FONT");
            }
        }
        // do not post quit here — closing the dialog must not terminate the whole app
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static std::string ReadFileToString(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream ss; ss << ifs.rdbuf();
    return ss.str();
}

static std::string GetSettingsIniPath() {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    std::string path;
    if (len > 0 && len < MAX_PATH) {
        path = std::string(buf) + "\\WinUpdate";
    } else {
        path = "."; // fallback to current dir
    }
    // ensure directory exists
    std::wstring wpath;
    int nw = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (nw > 0) {
        std::vector<wchar_t> wb(nw);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wb.data(), nw);
        wpath = wb.data();
        CreateDirectoryW(wpath.c_str(), NULL);
    }
    std::string ini = path + "\\wup_settings.ini";
    return ini;
}

static std::string LoadLocaleSetting() {
    std::string ini = GetSettingsIniPath();
    std::ifstream ifs(ini, std::ios::binary);
    if (!ifs) {
        // create default INI with required sections
        std::ofstream ofs(ini, std::ios::binary);
        if (ofs) {
            ofs << "[language]\n";
            ofs << "en\n\n";
            ofs << "[skipped]\n";
        }
        return std::string("en");
    }
    auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
    std::string line;
    bool inLang = false;
    while (std::getline(ifs, line)) {
        trim(line);
        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') {
            inLang = (line == "[language]");
            continue;
        }
        if (inLang) return line;
    }
    return std::string();
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

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

bool ShowSkipConfirm(HWND parent, const std::wstring &appname, const std::wstring &availableVersion) {
    AppendLog("[skip_confirm] ShowSkipConfirm entered\n");
    std::string locale = LoadLocaleSetting(); if (locale.empty()) locale = "en";

    // load templates from i18n; fall back to existing common keys if present
    // i18n files already contain `skip_confirm_question` and `skip_confirm_body` plus `btn_do_it`/`btn_cancel`.
    std::string question_t = LoadI18nValue(locale, "skip_confirm_question");
    std::string body_t = LoadI18nValue(locale, "skip_confirm_body");
    std::string btn_ok_t = LoadI18nValue(locale, "btn_do_it");
    std::string btn_cancel_t = LoadI18nValue(locale, "btn_cancel");

    // If i18n files do not provide values, fall back to English defaults.
    if (question_t.empty()) question_t = "Skip this update of <appname>?";
    if (body_t.empty()) body_t = "Version <available> will not show in the update list anymore.\nThe version after <available> will show, however.\nYou are skipping only that version.";
    if (btn_ok_t.empty()) btn_ok_t = "Do it!";
    if (btn_cancel_t.empty()) btn_cancel_t = "Cancel";

    auto replace_all = [](std::string s, const std::string &from, const std::string &to)->std::string {
        if (from.empty()) return s;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
        return s;
    };

    std::string appn_utf8;
    // convert appname and version to UTF-8
    int needed = WideCharToMultiByte(CP_UTF8, 0, appname.c_str(), -1, NULL, 0, NULL, NULL);
    if (needed>0) { std::vector<char> buf(needed); WideCharToMultiByte(CP_UTF8,0,appname.c_str(),-1,buf.data(),needed,NULL,NULL); appn_utf8 = buf.data(); }
    std::string ver_utf8;
    needed = WideCharToMultiByte(CP_UTF8, 0, availableVersion.c_str(), -1, NULL, 0, NULL, NULL);
    if (needed>0) { std::vector<char> buf(needed); WideCharToMultiByte(CP_UTF8,0,availableVersion.c_str(),-1,buf.data(),needed,NULL,NULL); ver_utf8 = buf.data(); }

    // replace placeholders in templates: <appname> and <available>
    std::string title_s = replace_all(question_t, "<appname>", appn_utf8);
    std::string content_s = replace_all(body_t, "<available>", ver_utf8);
    content_s = replace_all(content_s, "<appname>", appn_utf8);
    // convert literal escape sequences like "\n" in i18n files into real CRLFs for Windows dialogs
    content_s = replace_all(content_s, "\\n", "\r\n");

    // convert to wide for TaskDialog
    std::wstring wtitle = Utf8ToWide(title_s);
    std::wstring wcontent = Utf8ToWide(content_s);
    std::wstring wok = Utf8ToWide(btn_ok_t);
    std::wstring wcancel = Utf8ToWide(btn_cancel_t);

    // Prepare TaskDialog buttons
    TASKDIALOG_BUTTON buttons[2];
    buttons[0].nButtonID = 1001; buttons[0].pszButtonText = wok.c_str();
    buttons[1].nButtonID = 1002; buttons[1].pszButtonText = wcancel.c_str();

    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = parent;
    cfg.pszWindowTitle = wtitle.c_str();
    cfg.pszMainInstruction = wtitle.c_str();
    cfg.pszContent = wcontent.c_str();
    cfg.dwCommonButtons = 0;
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.pButtons = buttons;
    cfg.cButtons = 2;

    int buttonPressed = 0;
    // Ensure common controls are initialized (safe no-op if already initialized)
    INITCOMMONCONTROLSEX icc{}; icc.dwSize = sizeof(icc); icc.dwICC = ICC_STANDARD_CLASSES; InitCommonControlsEx(&icc);
    AppendLog("[skip_confirm] Prepared dialog strings, attempting TaskDialogIndirect\n");
    // Call TaskDialogIndirect dynamically to avoid issues on platforms where it may be missing
    HMODULE hComctl = LoadLibraryW(L"comctl32.dll");
        // fallback: create a small custom modal dialog so we can control button labels
        AppendLog("[skip_confirm] Using custom fallback dialog\n");
        static bool sClassRegistered = false;
        static const wchar_t *kFallbackClass = L"WUP_SkipFallback";
        if (!sClassRegistered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = SkipConfirmFallbackProc;
            wc.hInstance = GetModuleHandleW(NULL);
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
            wc.lpszClassName = kFallbackClass;
            RegisterClassExW(&wc);
            sClassRegistered = true;
        }
        int result = 0;
        // create centered dialog relative to parent (or screen)
        RECT prc{}; int px=0, py=0, pw=800, ph=220;
        if (parent && IsWindow(parent)) { GetWindowRect(parent, &prc); pw = 520; ph = 180; px = prc.left + ((prc.right-prc.left)-pw)/2; py = prc.top + ((prc.bottom-prc.top)-ph)/2; }
        else { RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0); pw = 520; ph = 180; px = (wa.left+wa.right-pw)/2; py = (wa.top+wa.bottom-ph)/2; }
        HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kFallbackClass, wtitle.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, px, py, pw, ph, parent, NULL, GetModuleHandleW(NULL), NULL);
        if (!hDlg) {
            // fallback to system MessageBox if we couldn't create dialog
            AppendLog("[skip_confirm] failed to create fallback dialog, using MessageBox\n");
            int mb2 = MessageBoxW(parent, wcontent.c_str(), wtitle.c_str(), MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
            bool r2 = (mb2 == IDYES);
            if (r2 && parent) {
                std::string payload = "WUP_SKIP\n" + appn_utf8 + "\n" + ver_utf8 + "\n";
                COPYDATASTRUCT cds{}; cds.dwData = 0x57475053; cds.cbData = (DWORD)(payload.size()+1); cds.lpData = (PVOID)payload.c_str();
                HWND target = FindWindowW(L"WinUpdateClass", NULL); if (!target) target = (HWND)parent; SendMessageA((HWND)target, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
            }
            return r2;
        }
        // set pointer for result storage
        SetPropW(hDlg, L"WUP_RES", (HANDLE)&result);
        // determine client area so controls are positioned within visible client rect
        RECT crect; GetClientRect(hDlg, &crect);
        int clientW = crect.right - crect.left;
        int clientH = crect.bottom - crect.top;
        // buttons (wider and with extra bottom padding)
        int btnW = 140, btnH = 34; int gap = 16;
        int paddingLeft = 12;
        int paddingTop = 12;
        int paddingBottom = 12; // ensure a few px padding below buttons
        int bx = clientW - (btnW*2 + gap + paddingLeft);
        if (bx < paddingLeft) bx = paddingLeft;
        int by = clientH - btnH - paddingBottom;
        // create content static above buttons, sized from client area
        int contentH = by - paddingTop - 8; // leave small gap between content and buttons
        if (contentH < 20) contentH = 20;
        HWND hStatic = CreateWindowExW(0, L"STATIC", wcontent.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, paddingLeft, paddingTop, clientW - (paddingLeft*2), contentH, hDlg, NULL, GetModuleHandleW(NULL), NULL);
        HWND hBtnOk = CreateWindowExW(0, L"BUTTON", wok.c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, bx, by, btnW, btnH, hDlg, (HMENU)1001, GetModuleHandleW(NULL), NULL);
        HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", wcancel.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, bx + btnW + gap, by, btnW, btnH, hDlg, (HMENU)1002, GetModuleHandleW(NULL), NULL);
        // create a bold larger font for the dialog text and buttons
        HDC hdcScreen = GetDC(NULL);
        int dpiY = GetDeviceCaps(hdcScreen, LOGPIXELSY);
        ReleaseDC(NULL, hdcScreen);
        int fontPt = 12; // point size
        int lfHeight = -MulDiv(fontPt, dpiY, 72);
        HFONT hFont = CreateFontW(lfHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (hFont) {
            SendMessageW(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hBtnOk, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
            SetPropW(hDlg, L"WUP_FONT", (HANDLE)hFont);
        }
        ShowWindow(hDlg, SW_SHOW);
        // run modal loop
        MSG msg;
        while (IsWindow(hDlg)) {
            if (GetMessage(&msg, NULL, 0, 0) <= 0) break;
            if (!IsWindow(hDlg) || !IsDialogMessage(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        }
        // read result (set by window proc)
        bool res = (result == 1);
        if (res) {
            // send WM_COPYDATA to main window so main persists the skip
            try {
                std::string payload = "WUP_SKIP\n" + appn_utf8 + "\n" + ver_utf8 + "\n";
                COPYDATASTRUCT cds{}; cds.dwData = 0x57475053; cds.cbData = (DWORD)(payload.size()+1); cds.lpData = (PVOID)payload.c_str();
                HWND target = FindWindowW(L"WinUpdateClass", NULL); if (!target) target = parent; SendMessageA((HWND)target, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
            } catch(...) {}
        }
        return res;
    
}
