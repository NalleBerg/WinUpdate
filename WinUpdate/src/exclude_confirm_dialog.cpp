#include "exclude_confirm_dialog.h"
#include "../resource.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include "logging.h"

// Window proc for the exclude confirm dialog
static LRESULT CALLBACK ExcludeConfirmProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        HANDLE h = GetPropW(hwnd, L"WUP_RES");
        int *p = h ? (int*)h : nullptr;
        if (id == 1001) { if (p) *p = 1; DestroyWindow(hwnd); return 0; }
        if (id == 1002) { if (p) *p = 0; DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        {
            HANDLE hf = GetPropW(hwnd, L"WUP_FONT");
            if (hf) {
                DeleteObject((HFONT)hf);
                RemovePropW(hwnd, L"WUP_FONT");
            }
        }
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
        path = ".";
    }
    std::wstring wpath;
    int nw = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, NULL, 0);
    if (nw > 0) {
        std::vector<wchar_t> wb(nw);
        MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wb.data(), nw);
        wpath = wb.data();
        CreateDirectoryW(wpath.c_str(), NULL);
    }
    std::string ini = path + "\\wup_settings.ini";
    return ini;
}

static std::string LoadLocaleSetting() {
    std::string ini = GetSettingsIniPath();
    std::ifstream ifs(ini, std::ios::binary);
    if (!ifs) return std::string("en_GB");
    
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
    return std::string("en_GB");
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

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

bool ShowExcludeConfirm(HWND parent, const std::wstring &appname) {
    std::string locale = LoadLocaleSetting(); if (locale.empty()) locale = "en_GB";

    std::string question_t = LoadI18nValue(locale, "exclude_confirm_question");
    std::string body_t = LoadI18nValue(locale, "exclude_confirm_body");
    std::string btn_ok_t = LoadI18nValue(locale, "btn_do_it");
    std::string btn_cancel_t = LoadI18nValue(locale, "btn_cancel");

    if (question_t.empty()) question_t = "Exclude this app from all future scans of <appname>?";
    if (body_t.empty()) body_t = "The app will not appear in update lists anymore.\\nYou can re-include it later from Settings.";
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
    int needed = WideCharToMultiByte(CP_UTF8, 0, appname.c_str(), -1, NULL, 0, NULL, NULL);
    if (needed>0) { std::vector<char> buf(needed); WideCharToMultiByte(CP_UTF8,0,appname.c_str(),-1,buf.data(),needed,NULL,NULL); appn_utf8 = buf.data(); }

    std::string title_s = replace_all(question_t, "<appname>", appn_utf8);
    std::string content_s = replace_all(body_t, "<appname>", appn_utf8);
    // convert literal escape sequences like "\n" in i18n files into real CRLFs for Windows dialogs
    content_s = replace_all(content_s, "\\n", "\r\n");

    std::wstring wtitle = Utf8ToWide(title_s);
    std::wstring wcontent = Utf8ToWide(content_s);
    std::wstring wok = Utf8ToWide(btn_ok_t);
    std::wstring wcancel = Utf8ToWide(btn_cancel_t);

    // Use fallback custom dialog (same style as skip confirmation)
    static bool sClassRegistered = false;
    static const wchar_t *kFallbackClass = L"WUP_ExcludeFallback";
    if (!sClassRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ExcludeConfirmProc;
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
        int mb2 = MessageBoxW(parent, wcontent.c_str(), wtitle.c_str(), MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
        return (mb2 == IDYES);
    }
    
    // Set app icon
    HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (hIcon) {
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    
    // set pointer for result storage
    SetPropW(hDlg, L"WUP_RES", (HANDLE)&result);
    // determine client area so controls are positioned within visible client rect
    RECT crect; GetClientRect(hDlg, &crect);
    int clientW = crect.right - crect.left;
    int clientH = crect.bottom - crect.top;
    // buttons (wider and with extra bottom padding)
    int btnW = 140, btnH = 32; int gap = 16;
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
    // Bring main window to front if cancelled
    if (!res && parent) {
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
        SetActiveWindow(parent);
    }
    return res;
}
