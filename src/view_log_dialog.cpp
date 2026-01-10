#include "view_log_dialog.h"
#include "Config.h"
#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>

// Simple i18n loader
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

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

struct DialogContext {
    HWND parent;
    HWND hEdit;
    std::string locale;
};

static LRESULT CALLBACK ViewLogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogContext *ctx = (DialogContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        if (!ctx) return DefWindowProcW(hWnd, msg, wParam, lParam);
        if (id == 1001) { // Close
            if (ctx && ctx->parent && IsWindow(ctx->parent)) {
                PostMessageW(ctx->parent, (WM_APP+7), 0, 0);
                SetForegroundWindow(ctx->parent);
                BringWindowToTop(ctx->parent);
            }
            DestroyWindow(hWnd);
            return 0;
        }
    }
    if (msg == WM_CLOSE) {
        if (ctx && ctx->parent && IsWindow(ctx->parent)) {
            SetForegroundWindow(ctx->parent);
            BringWindowToTop(ctx->parent);
        }
        DestroyWindow(hWnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool ShowInstallLogDialog(HWND parent, const std::string &locale) {
    // Load the install log
    std::string log = LoadInstallLog();
    
    // If no log exists, show message and return
    if (log.empty()) {
        std::string msg = LoadI18nValue(locale, "no_install_log");
        if (msg.empty()) msg = "No install log available yet.";
        std::wstring title = Utf8ToWide(LoadI18nValue(locale, "app_title"));
        if (title.empty()) title = L"WinUpdate";
        MessageBoxW(parent, Utf8ToWide(msg).c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
        return false;
    }

    // Create dialog window
    static const wchar_t *kClass = L"WUP_ViewLogDlg";
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{}; 
        wc.cbSize = sizeof(wc); 
        wc.lpfnWndProc = ViewLogWndProc; 
        wc.hInstance = GetModuleHandleW(NULL); 
        wc.lpszClassName = kClass; 
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        RegisterClassExW(&wc); 
        reg = true;
    }
    
    int W = 700, H = 500;
    RECT prc{}; int px=100, py=100;
    if (parent && IsWindow(parent)) { 
        GetWindowRect(parent, &prc); 
        px = prc.left + 60; 
        py = prc.top + 60; 
    }
    
    std::wstring title = Utf8ToWide(LoadI18nValue(locale, "install_log_title"));
    if (title.empty()) title = L"Install Log";
    
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, title.c_str(), 
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX, 
        px, py, W, H, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!hDlg) return false;
    
    // Get actual client area size (excludes title bar and borders)
    RECT clientRect;
    GetClientRect(hDlg, &clientRect);
    int clientW = clientRect.right - clientRect.left;
    int clientH = clientRect.bottom - clientRect.top;
    
    DialogContext *ctx = new DialogContext();
    ctx->parent = parent;
    ctx->locale = locale;
    
    // Load RichEdit library
    LoadLibraryW(L"Riched20.dll");
    
    // Create RichEdit control for RTF display (use client height for sizing)
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit20W", NULL, 
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 
        12, 12, clientW-24, clientH-58, hDlg, NULL, GetModuleHandleW(NULL), NULL);
    
    // Enable RTF mode
    SendMessageW(hEdit, EM_SETTEXTMODE, TM_RICHTEXT, 0);
    
    // Display log as RTF
    struct StreamData {
        const char* data;
        size_t size;
        size_t pos;
    } streamData;
    streamData.data = log.c_str();
    streamData.size = log.size();
    streamData.pos = 0;
    
    EDITSTREAM es{};
    es.dwCookie = (DWORD_PTR)&streamData;
    es.pfnCallback = [](DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb) -> DWORD {
        StreamData* pData = (StreamData*)dwCookie;
        LONG bytesToCopy = (std::min)(cb, (LONG)(pData->size - pData->pos));
        if (bytesToCopy > 0) {
            memcpy(pbBuff, pData->data + pData->pos, bytesToCopy);
            pData->pos += bytesToCopy;
        }
        *pcb = bytesToCopy;
        return 0;
    };
    
    SendMessageW(hEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    
    ctx->hEdit = hEdit;
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);
    
    // Close button (centered, positioned in client area)
    std::string txtClose = LoadI18nValue(locale, "btn_cancel");
    if (txtClose.empty()) txtClose = "Close";
    std::wstring wclose = Utf8ToWide(txtClose);
    int btnWidth = 100;
    int btnHeight = 30;
    int btnX = (clientW - btnWidth) / 2;  // Center horizontally in client area
    int btnY = clientH - 38;  // 8 pixels from bottom of client area
    HWND hBtnClose = CreateWindowExW(0, L"BUTTON", wclose.c_str(), 
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 
        btnX, btnY, btnWidth, btnHeight, hDlg, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    
    ShowWindow(hDlg, SW_SHOW);
    
    // Modal message loop
    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        if (IsDialogMessage(hDlg, &msg)) continue;
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    if (ctx) {
        delete ctx;
        ctx = nullptr;
    }
    
    return true;
}
