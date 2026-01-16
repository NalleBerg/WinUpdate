#include "About.h"
#include "ctrlw.h"
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <windows.h>
#include <sstream>
#include <richedit.h>
#include <shlobj.h>

// Forward declaration for t() function from main.cpp
extern std::wstring t(const char *key);

// Set the published timestamp here. Update before publishing releases on the website.
const wchar_t ABOUT_PUBLISHED[] = L"2026-01-06";
const wchar_t ABOUT_VERSION[] = L"2026.01.06.11";

// Helper function to append formatted text to RichEdit control
static void AppendFormattedText(HWND hRichEdit, const std::wstring& text, bool isBold, COLORREF color, int fontSize = 0, bool centered = false)
{
    int len = GetWindowTextLengthW(hRichEdit);
    SendMessageW(hRichEdit, EM_SETSEL, len, len);
    
    // Set paragraph formatting for centering
    if (centered) {
        PARAFORMAT2 pf = {};
        pf.cbSize = sizeof(PARAFORMAT2);
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_CENTER;
        SendMessageW(hRichEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    if (fontSize > 0) {
        cf.dwMask |= CFM_SIZE;
        cf.yHeight = fontSize * 20; // Size is in twips (1/20th of a point)
    }
    cf.crTextColor = color;
    cf.dwEffects = isBold ? CFE_BOLD : 0;
    
    SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hRichEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    
    // Reset to left alignment after centered text
    if (centered) {
        PARAFORMAT2 pf = {};
        pf.cbSize = sizeof(PARAFORMAT2);
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_LEFT;
        SendMessageW(hRichEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
}

// Get the path to the cache file for winget package count
static std::wstring GetPackageCountCachePath()
{
    wchar_t appData[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData) != S_OK) {
        return L"";
    }
    std::wstring dir = std::wstring(appData) + L"\\WinUpdate";
    CreateDirectoryW(dir.c_str(), NULL);
    return dir + L"\\winget_package_count.txt";
}

// Format number according to locale: UK uses commas (11,107), Norwegian uses spaces (11 107)
static std::wstring FormatPackageCount(int count, bool useComma)
{
    std::wstring num = std::to_wstring(count);
    std::wstring result;
    int len = (int)num.length();
    for (int i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) {
            result += useComma ? L"," : L" ";
        }
        result += num[i];
    }
    return result;
}

// Get winget package count from cache
static int GetCachedPackageCount()
{
    std::wstring cachePath = GetPackageCountCachePath();
    if (cachePath.empty()) return -1;
    
    std::ifstream file(cachePath.c_str());
    if (!file.is_open()) return -1;
    
    int count = -1;
    file >> count;
    return count;
}

// Save winget package count to cache
static void SavePackageCount(int count)
{
    std::wstring cachePath = GetPackageCountCachePath();
    if (cachePath.empty()) return;
    
    std::ofstream file(cachePath.c_str());
    if (file.is_open()) {
        file << count;
    }
}

// Fetch current winget package count by running winget command
static int FetchWingetPackageCount()
{
    // Create a pipe for reading command output
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return -1;
    }
    
    // Make sure the read handle is not inherited
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    
    // Set up process startup info
    STARTUPINFOW si = {};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;
    
    PROCESS_INFORMATION pi = {};
    
    // Run: cmd /c winget search "" 2>nul | find /c /v ""
    // This counts all non-empty lines
    wchar_t cmdLine[] = L"cmd /c winget search \"\" 2>nul | find /c /v \"\"";
    
    BOOL success = CreateProcessW(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hWritePipe);
    
    if (!success) {
        CloseHandle(hReadPipe);
        return -1;
    }
    
    // Wait for process to complete (with timeout)
    WaitForSingleObject(pi.hProcess, 30000); // 30 second timeout
    
    // Read output
    char buffer[128];
    DWORD bytesRead;
    std::string output;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }
    
    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    // Parse the count (subtract 2 for header lines)
    int totalLines = atoi(output.c_str());
    int packageCount = totalLines > 2 ? totalLines - 2 : 0;
    
    return packageCount;
}

// Get winget package count - fetch if possible, use cache as fallback
static int GetWingetPackageCount()
{
    int count = FetchWingetPackageCount();
    
    if (count > 0) {
        // Successfully fetched, save to cache
        SavePackageCount(count);
        return count;
    }
    
    // Failed to fetch, try cache
    count = GetCachedPackageCount();
    return count > 0 ? count : 11107; // Fallback to known count
}

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

// Window procedure for License dialog
static LRESULT CALLBACK LicenseDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                PostQuitMessage(0);
                return 0;
            }
            break;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Window procedure for About dialog
static LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                PostQuitMessage(0);
                return 0;
            }
            if (LOWORD(wParam) == 1001) {
                ShowLicenseDialog(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Show the license popup (modal)
void ShowLicenseDialog(HWND parent)
{
    LoadLibraryW(L"Riched20.dll");
    
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
    WNDCLASSW wc{}; wc.lpfnWndProc = LicenseDlgProc; wc.hInstance = hi; wc.lpszClassName = L"WUPLicenseDlgClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) {
        wc.lpfnWndProc = LicenseDlgProc; wc.hInstance = hi; wc.lpszClassName = L"WUPLicenseDlgClass";
        RegisterClassW(&wc);
    }
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
    
    // license text - RichEdit for formatting support
    std::wstring wmd = Utf8ToWide(md);
    HWND he = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit20W", NULL, 
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY | ES_WANTRETURN, 
        12, 144, W-24, H-200, dlg, NULL, hi, NULL);
    SendMessageW(he, EM_SETTARGETDEVICE, 0, 0); // Enable word wrap
    SendMessageW(he, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    SendMessageW(he, EM_SETSEL, 0, 0);
    SendMessageW(he, EM_REPLACESEL, FALSE, (LPARAM)wmd.c_str());
    
    // close button
    HWND hb = CreateWindowExW(0, L"Button", t("about_close").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, W/2 - 50, H-44, 100, 28, dlg, (HMENU)IDOK, hi, NULL);
    
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(hb);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // Handle Ctrl+W
        if (HandleCtrlW(dlg, msg.message, msg.wParam, msg.lParam)) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    if (hbmp) DeleteObject(hbmp);
    DestroyWindow(dlg);
    
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
    }
}

// ---------------------- About dialog (file scope) ----------------------
void ShowAboutDialog(HWND parent)
{
    LoadLibraryW(L"Riched20.dll");
    
    // Create a simple centered modal window with formatted controls
    const int W = 680, H = 580;
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc{};
    wc.lpfnWndProc = AboutDlgProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"WUPAboutDlgClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) {
        wc.lpfnWndProc = AboutDlgProc;
        wc.hInstance = hi;
        wc.lpszClassName = L"WUPAboutDlgClass";
        RegisterClassW(&wc);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"About WinUpdate", WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H, parent, NULL, hi, NULL);
    if (!dlg) { MessageBoxW(parent, L"Unable to create About window.", L"About", MB_OK | MB_ICONERROR); return; }

    // Create single RichEdit control that fills most of the window with scrollbar
    HWND he = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit20W", NULL, 
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY | ES_WANTRETURN, 
        10, 10, W-20, H-70, dlg, NULL, hi, NULL);
    SendMessageW(he, EM_SETTARGETDEVICE, 0, 0); // Enable word wrap
    SendMessageW(he, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    
    // Format the About text with colors and bold using i18n
    AppendFormattedText(he, t("about_title") + L"\r\n", true, RGB(0, 70, 140), 14, true);  // Bold dark blue title, centered
    AppendFormattedText(he, t("about_subtitle") + L"\r\n", false, RGB(70, 70, 70), 9, true);  // Gray subtitle, centered
    AppendFormattedText(he, t("about_published") + L" ", true, RGB(0, 0, 0));
    AppendFormattedText(he, std::wstring(ABOUT_PUBLISHED) + L"\r\n", false, RGB(0, 0, 0));
    AppendFormattedText(he, t("about_version") + L" ", true, RGB(0, 0, 0));
    AppendFormattedText(he, std::wstring(ABOUT_VERSION) + L"\r\n\r\n", false, RGB(0, 0, 0));
    
    // Get dynamic package count and format according to locale
    int packageCount = GetWingetPackageCount();
    std::wstring desc1 = t("about_description");
    
    // Determine locale for number formatting
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH];
    GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH);
    std::wstring locale(localeName);
    
    // Norwegian locales use space separator, English use comma
    bool useComma = (locale.find(L"nb") != 0 && locale.find(L"no") != 0 && locale.find(L"nn") != 0);
    std::wstring formattedCount = FormatPackageCount(packageCount, useComma);
    
    // Replace "over 11,000" or "over 11 000" with actual count
    size_t pos = desc1.find(L"over 11,000 packages");
    if (pos != std::wstring::npos) {
        desc1.replace(pos, 20, formattedCount + L" packages");
    } else {
        pos = desc1.find(L"over 11 000 pakker");
        if (pos != std::wstring::npos) {
            desc1.replace(pos, 18, formattedCount + L" pakker");
        }
    }
    
    AppendFormattedText(he, desc1 + L"\r\n", false, RGB(50, 50, 50));
    
    std::wstring desc2 = t("about_description2");
    AppendFormattedText(he, desc2 + L"\r\n\r\n", false, RGB(50, 50, 50));
    
    AppendFormattedText(he, t("about_author") + L" ", true, RGB(0, 0, 0));
    AppendFormattedText(he, L"Nalle Berg\r\n", false, RGB(0, 0, 0));
    AppendFormattedText(he, t("about_copyleft") + L" ", true, RGB(0, 0, 0));
    AppendFormattedText(he, L"2025\r\n", false, RGB(0, 0, 0));
    AppendFormattedText(he, t("about_license") + L" ", true, RGB(0, 0, 0));
    AppendFormattedText(he, t("about_license_name") + L" — ", false, RGB(0, 100, 0));
    AppendFormattedText(he, t("about_license_file") + L"\r\n\r\n", false, RGB(0, 100, 0));
    
    AppendFormattedText(he, t("about_summary") + L"\r\n", true, RGB(0, 0, 0));
    AppendFormattedText(he, t("about_summary_text") + L"\r\n\r\n", false, RGB(70, 70, 70));
    
    AppendFormattedText(he, t("about_attribution") + L"\r\n", false, RGB(100, 100, 100));

    // Buttons positioned at bottom of window
    HWND vb = CreateWindowExW(0, L"Button", t("about_view_license").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, H-50, 150, 35, dlg, (HMENU)1001, hi, NULL);
    HWND cb = CreateWindowExW(0, L"Button", t("about_close").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, W-160, H-50, 150, 35, dlg, (HMENU)IDOK, hi, NULL);

    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(cb);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // Handle Ctrl+W
        if (HandleCtrlW(dlg, msg.message, msg.wParam, msg.lParam)) {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    DestroyWindow(dlg);
    
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
    }
}
