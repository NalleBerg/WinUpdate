#include "About.h"
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <windows.h>
#include <sstream>

// Set the published timestamp here. Update before publishing releases on the website.
const wchar_t ABOUT_PUBLISHED[] = L"2026-01-02";

static std::wstring BuildAboutText()
{
    std::wstring eng = L"WinUpdate\r\n";
    eng += L"(Win as in Victory!)\r\n";
    eng += L"Published: "; eng += ABOUT_PUBLISHED; eng += L"\r\n\r\n";
    eng += L"A lightweight Win32 GUI wrapper for Microsoft\'s winget.\r\n";
    eng += L"Select upgrades, run installs under a single elevation, and review detailed output.\r\n\r\n";
    eng += L"Author: Nalle Berg\r\n";
    eng += L"Copyleft 2025\r\n";
    eng += L"License: MIT License — see LICENSE.md included with this distribution.\r\n\r\n";
    eng += L"Summary: You are free to use, copy, modify, merge, publish, distribute, sublicense,\r\n";
    eng += L"and/or sell copies of the Software, provided the copyright and license notice\r\n";
    eng += L"are included in all substantial portions of the Software.\r\n\r\n";
    eng += L"If you mention a newer version or fork, please attribute the author as requested.\r\n\r\n";

    std::wstring no = L"\r\nOm WinUpdate\r\n";
    no += L"(Win som i seier!)\r\n";
    no += L"Publisert: "; no += ABOUT_PUBLISHED; no += L"\r\n\r\n";
    no += L"Et lettvint Win32-grensesnitt for Microsoft\'s winget.\r\n";
    no += L"Velg oppdateringer, installer med én forhøyelse, og se detaljert logg.\r\n\r\n";
    no += L"Forfatter: Nalle Berg\r\n";
    no += L"Copyleft 2025\r\n";
    no += L"Lisens: MIT License — se LICENSE.md inkludert i distribusjonen.\r\n\r\n";
    no += L"Sammendrag: Du står fritt til å bruke, kopiere, endre, slå sammen, publisere,\r\n";
    no += L"distribuere, gi underlisens, og/eller selge kopier av programvaren, forutsatt at\r\n";
    no += L"opphavsretts- og lisensmerknaden er inkludert i alle vesentlige deler.\r\n\r\n";
    no += L"Hvis du nevner en nyere versjon eller et fork, vennligst gi forfatteren attributt som\r\n";
    no += L"spesifisert.\r\n\r\n";

    return eng + no;
}
// ---------------------- Helper utilities (file scope) ----------------------
static std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

static std::string ReadFileUtf8(const std::wstring &path) {
    std::string narrow = WideToUtf8(path);
    std::ifstream ifs(narrow, std::ios::binary);
    if (!ifs) return std::string();
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::vector<unsigned char> Base64Decode(const std::string &in) {
    std::string s = in;
    std::vector<int> T(256, -1);
    const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) T[(unsigned char)chars[i]] = i;
    std::vector<unsigned char> out;
    int val=0, valb=-8;
    for (unsigned char c : s) {
        if (T[c] == -1) break;
        val = (val<<6) + T[c];
        valb += 6;
        if (valb>=0) {
            out.push_back((unsigned char)((val>>valb)&0xFF));
            valb-=8;
        }
    }
    return out;
}

// Extract base64 payload from data URI in LICENSE.md and write a PNG file to temp location.
static std::wstring SaveEmbeddedLogoToTemp()
{
    std::string md = ReadFileUtf8(std::wstring(L"LICENSE.md"));
    size_t p = md.find("data:image/png;base64,");
    if (p == std::string::npos) return std::wstring();
    p += strlen("data:image/png;base64,");
    size_t e = md.find(')', p);
    std::string b64 = md.substr(p, (e==std::string::npos)? std::string::npos : (e - p));
    // strip whitespace
    b64.erase(std::remove_if(b64.begin(), b64.end(), [](unsigned char c){ return isspace(c); }), b64.end());
    auto bytes = Base64Decode(b64);
    if (bytes.empty()) return std::wstring();
    wchar_t tmpPath[MAX_PATH]; GetTempPathW(MAX_PATH, tmpPath);
    std::wstring out = std::wstring(tmpPath) + L"wup_mit_logo.png";
    std::ofstream ofs(WideToUtf8(out), std::ios::binary);
    if (!ofs) return std::wstring();
    ofs.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    ofs.close();
    return out;
}

// Show the license popup (modal)
void ShowLicenseDialog(HWND parent)
{
    std::string md = ReadFileUtf8(std::wstring(L"LICENSE.md"));
    if (md.empty()) {
        MessageBoxW(parent, L"License file not found.", L"License", MB_OK | MB_ICONERROR);
        return;
    }
    // save embedded logo to temp file
    std::wstring logoPath = SaveEmbeddedLogoToTemp();

    const int W = 640, H = 520;
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2; int py = (pr.bottom + pr.top) / 2;
    int x = (px==0)? CW_USEDEFAULT : (px - W/2); int y = (py==0)? CW_USEDEFAULT : (py - H/2);
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hi; wc.lpszClassName = L"WUPLicenseDlgClass"; RegisterClassW(&wc);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"License", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) { MessageBoxW(parent, L"Unable to create license window.", L"License", MB_OK | MB_ICONERROR); return; }
    // image static
    HWND him = CreateWindowExW(0, L"Static", NULL, WS_CHILD | WS_VISIBLE | SS_BITMAP, (W-128)/2, 8, 128, 128, dlg, NULL, hi, NULL);
    HBITMAP hbmp = NULL;
    if (!logoPath.empty()) {
        // LoadImage expects BMP for STM_SETIMAGE; convert PNG -> bitmap via LoadImage from file won't work for PNG.
        // As fallback, we simply leave the static empty; users can open LICENSE.md to view the embedded image.
        hbmp = (HBITMAP)LoadImageW(NULL, logoPath.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        if (hbmp) SendMessageW(him, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hbmp);
    }
    // license text edit
    std::wstring wmd = Utf8ToWide(md);
    HWND he = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 12, 144, W-24, H-200, dlg, NULL, hi, NULL);
    SendMessageW(he, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(he, EM_SETSEL, 0, 0);
    SendMessageW(he, EM_REPLACESEL, FALSE, (LPARAM)wmd.c_str());
    // close button
    HWND hb = CreateWindowExW(0, L"Button", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, W/2 - 50, H-44, 100, 28, dlg, (HMENU)IDOK, hi, NULL);
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG msg; BOOL running = TRUE;
    while (running && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == dlg) {
            if (msg.message == WM_COMMAND) {
                int id = LOWORD(msg.wParam);
                if (id == IDOK) { running = FALSE; DestroyWindow(dlg); break; }
            }
        }
        TranslateMessage(&msg); DispatchMessage(&msg);
    }
    if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
    if (hbmp) DeleteObject(hbmp);
}

// ---------------------- About dialog (file scope) ----------------------
void ShowAboutDialog(HWND parent)
{
    // Create a simple centered modal window with formatted controls
    const int W = 620, H = 380;
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hi;
    wc.lpszClassName = L"WUPAboutDlgClass";
    RegisterClassW(&wc);

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"About WinUpdate", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) { MessageBoxW(parent, L"Unable to create About window.", L"About", MB_OK | MB_ICONERROR); return; }

    std::wstring txt = BuildAboutText();
    HWND he = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 12, 12, W-24, H-84, dlg, NULL, hi, NULL);
    SendMessageW(he, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(he, EM_SETSEL, 0, 0);
    SendMessageW(he, EM_REPLACESEL, FALSE, (LPARAM)txt.c_str());

    HWND vb = CreateWindowExW(0, L"Button", L"View License", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12, H-56, 120, 32, dlg, (HMENU)1001, hi, NULL);
    HWND cb = CreateWindowExW(0, L"Button", L"Close", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, W-120-12, H-56, 120, 32, dlg, (HMENU)IDOK, hi, NULL);

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    MSG msg; BOOL running = TRUE;
    while (running && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == dlg) {
            if (msg.message == WM_COMMAND) {
                int id = LOWORD(msg.wParam);
                if (id == IDOK) { running = FALSE; DestroyWindow(dlg); break; }
                if (id == 1001) { ShowLicenseDialog(dlg); }
            }
        }
        TranslateMessage(&msg); DispatchMessage(&msg);
    }
    if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);
}
