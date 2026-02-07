// ============================================================================
// ABOUT DIALOG - WinProgramManager
// Complete Windows Package Management System - About Information
// Shows version, author, and application-specific usage details
// Includes WPM logo at top with GDI+ rendering and scroll support
// ============================================================================

#include "about.h"
#include "resource.h"
#include "app_details.h"
#include <windows.h>
#include <richedit.h>
#include <string>
#include <sstream>
#include <shlobj.h>
#include <gdiplus.h>

using namespace Gdiplus;

// ============================================================================
// VERSION INFORMATION
// Updated: 2026-02-07
// ============================================================================
const wchar_t ABOUT_PUBLISHED[] = L"07.02.2026";
const wchar_t ABOUT_VERSION[] = L"2026.02.07.08";

// External locale
extern Locale g_locale;

// Forward declarations
static LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void AppendRichText(HWND hEdit, const std::wstring& text, bool bold, COLORREF color, int fontSize = 9, bool centered = false);
std::wstring FormatNumber(int num, bool useComma);

// Global for logo image
static Image* g_logoImage = nullptr;
static HWND g_hEditCtrl = NULL;

// ============================================================================
// RICH TEXT FORMATTING HELPER
// Appends formatted text to RichEdit20W control with color, style, and alignment
// Parameters:
//   - hEdit: Handle to RichEdit control
//   - text: Text content to append
//   - bold: Apply bold formatting
//   - color: RGB text color
//   - fontSize: Font size in points
//   - centered: Center text alignment
// ============================================================================
void AppendRichText(HWND hEdit, const std::wstring& text, bool bold, COLORREF color, int fontSize, bool centered) {
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(CHARFORMAT2W);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE | CFM_FACE;
    cf.crTextColor = color;
    cf.dwEffects = bold ? CFE_BOLD : 0;
    cf.yHeight = fontSize * 20; // twips (1/1440 inch)
    wcscpy_s(cf.szFaceName, L"Segoe UI");
    
    // Get current text length and select end
    GETTEXTLENGTHEX gtl = {};
    gtl.flags = GTL_DEFAULT;
    gtl.codepage = 1200; // Unicode
    LONG len = (LONG)SendMessageW(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    SendMessageW(hEdit, EM_SETSEL, len, len);
    
    // Apply paragraph formatting if centered
    if (centered) {
        PARAFORMAT2 pf = {};
        pf.cbSize = sizeof(PARAFORMAT2);
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_CENTER;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
    
    // Set character format and insert text
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    
    // Reset to left alignment
    if (centered) {
        PARAFORMAT2 pf = {};
        pf.cbSize = sizeof(PARAFORMAT2);
        pf.dwMask = PFM_ALIGNMENT;
        pf.wAlignment = PFA_LEFT;
        SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    }
}

// ============================================================================
// NUMBER FORMATTING HELPER
// Formats integer with thousands separator (comma or space based on locale)
// Example: 10692 -> "10,692" (English) or "10 692" (Norwegian)
// ============================================================================
std::wstring FormatNumber(int num, bool useComma) {
    std::wstring str = std::to_wstring(num);
    std::wstring result;
    int count = 0;
    for (int i = str.length() - 1; i >= 0; i--) {
        if (count == 3) {
            result = (useComma ? L"," : L" ") + result;
            count = 0;
        }
        result = str[i] + result;
        count++;
    }
    return result;
}

// ============================================================================
// RICHEDIT SUBCLASS PROCEDURE
// Draws WPM logo at top of scrollable content
// Logo scrolls with text to maintain visual consistency
// ============================================================================
static WNDPROC g_origEditProc = NULL;

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT && g_logoImage) {
        // Let RichEdit draw first
        CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
        
        // Draw logo on top
        HDC hdc = GetDC(hwnd);
        Graphics graphics(hdc);
        
        // Get scroll position
        POINT pt = {0, 0};
        SendMessageW(hwnd, EM_GETSCROLLPOS, 0, (LPARAM)&pt);
        
        // Draw logo centered at top (scaled to 75%), offset by scroll
        RECT rc;
        GetClientRect(hwnd, &rc);
        int logoW = (int)(g_logoImage->GetWidth() * 0.75);
        int logoH = (int)(g_logoImage->GetHeight() * 0.75);
        int x = (rc.right - logoW) / 2;
        int y = 10 - pt.y; // Offset by scroll position
        
        // Only draw if at least partially visible
        if (y + logoH > 0 && y < rc.bottom) {
            graphics.DrawImage(g_logoImage, x, y, logoW, logoH);
        }
        
        ReleaseDC(hwnd, hdc);
        return 0;
    }
    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

// ============================================================================
// ABOUT DIALOG WINDOW PROCEDURE
// Handles button clicks and window close events
// ============================================================================
static LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                PostQuitMessage(0);
                return 0;
            }
            if (LOWORD(wParam) == 1001) { // View License button
                ShowLicenseDialog(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                PostQuitMessage(0);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// MAIN ABOUT DIALOG FUNCTION
// Creates and displays modal About dialog with:
// - WPM logo at top (scrollable)
// - Version and publish date
// - Shared WinProgramSuite description
// - WinProgramManager-specific usage information
// - License and Close buttons
// ============================================================================
void ShowAboutDialog(HWND parent) {
    LoadLibraryW(L"Riched20.dll");
    
    // Initialize GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    // Load WPM logo image with transparency
    wchar_t logoPath[MAX_PATH];
    GetModuleFileNameW(NULL, logoPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(logoPath, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = 0;
        wcscat_s(logoPath, L"wpm_logo.png");
    }
    g_logoImage = Image::FromFile(logoPath);
    
    // Create window
    const int W = 420, H = 560;
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);
    
    // Ensure dialog is not positioned above screen (minimum 30px from top)
    if (y < 30) y = 30;

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = AboutDlgProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WPMAboutDlgClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) {
        RegisterClassW(&wc);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, 
        wc.lpszClassName, g_locale.about_title.c_str(), 
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        x, y, W, H, parent, NULL, hi, NULL);
    
    if (!dlg) {
        MessageBoxW(parent, L"Unable to create About window.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Set app icon
    HICON hIcon = LoadIcon(hi, MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIcon) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }

    // Create RichEdit control - fills entire area, logo will scroll with content
    HWND hEdit = CreateWindowExW(0, L"RichEdit20W", NULL,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        10, 10, W - 20, H - 80,
        dlg, (HMENU)100, hi, NULL);
    
    g_hEditCtrl = hEdit;
    
    if (!hEdit) {
        DestroyWindow(dlg);
        return;
    }
    
    // Subclass the RichEdit to draw logo
    g_origEditProc = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0); // Enable word wrap
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_LINK);
    
    // Add space for logo at top
    int logoH = g_logoImage ? (int)(g_logoImage->GetHeight() * 0.75) : 0;
    int logoSpaceLines = (logoH + 20) / 15; // Approximate lines needed
    for (int i = 0; i < logoSpaceLines; i++) {
        AppendRichText(hEdit, L"\r\n", false, RGB(0, 0, 0), 9, false);
    }
    
    // Build formatted About content
    AppendRichText(hEdit, L"WinProgramSuite\r\n", true, RGB(0, 70, 140), 16, true);
    AppendRichText(hEdit, g_locale.about_subtitle + L"\r\n\r\n", false, RGB(80, 80, 80), 10, true);
    
    // Version info divider
    AppendRichText(hEdit, L"═════════════════════════════\r\n", false, RGB(100, 140, 180), 9, true);
    AppendRichText(hEdit, g_locale.about_published + L" ", true, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, std::wstring(ABOUT_PUBLISHED) + L"\r\n", false, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, g_locale.about_version + L" ", true, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, std::wstring(ABOUT_VERSION) + L"\r\n", false, RGB(0, 0, 0), 9, true);
    AppendRichText(hEdit, L"═════════════════════════════\r\n\r\n", false, RGB(100, 140, 180), 9, true);
    
    // Shared description
    AppendRichText(hEdit, g_locale.about_suite_desc + L"\r\n\r\n", false, RGB(40, 40, 40), 9, false);
    
    // Author and copyleft
    AppendRichText(hEdit, g_locale.about_author + L" ", true, RGB(0, 0, 0), 9, false);
    AppendRichText(hEdit, L"Nalle Berg\r\n", false, RGB(40, 40, 40), 9, false);
    AppendRichText(hEdit, g_locale.about_copyright + L" 2026 Nalle Berg\r\n\r\n", false, RGB(40, 40, 40), 9, false);
    
    // WinProgramManager-specific usage
    AppendRichText(hEdit, g_locale.about_wpm_title + L"\r\n", true, RGB(0, 70, 140), 11, false);
    AppendRichText(hEdit, g_locale.about_wpm_usage + L"\r\n\r\n", false, RGB(40, 40, 40), 9, false);
    
    // License divider
    AppendRichText(hEdit, L"═════════════════════════════\r\n\r\n", false, RGB(100, 140, 180), 9, true);
    AppendRichText(hEdit, g_locale.about_license_info + L"\r\n\r\n", true, RGB(0, 70, 140), 9, false);
    AppendRichText(hEdit, g_locale.about_github + L"\r\n", false, RGB(0, 100, 200), 9, false);
    AppendRichText(hEdit, L"https://github.com/NalleBerg/WinProgramSuite\r\n\r\n", false, RGB(40, 40, 40), 9, false);
    
    // Scroll to top
    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    
    // Create buttons at bottom (centered with gap)
    int btnY = H - 65;
    int btnWidth = 90;
    int btnGap = 10;
    int totalWidth = btnWidth * 2 + btnGap;
    int startX = (W - totalWidth) / 2;
    
    HWND btnLicense = CreateWindowExW(0, L"Button", g_locale.about_view_license.c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        startX, btnY, btnWidth, 30,
        dlg, (HMENU)1001, hi, NULL);
    
    HWND btnClose = CreateWindowExW(0, L"Button", g_locale.about_close.c_str(),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        startX + btnWidth + btnGap, btnY, btnWidth, 30,
        dlg, (HMENU)IDOK, hi, NULL);
    
    // Set font for buttons
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(btnLicense, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(btnClose, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Modal loop
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(btnClose);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            break;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    DestroyWindow(dlg);
    
    // Cleanup GDI+ resources
    if (g_logoImage) {
        delete g_logoImage;
        g_logoImage = nullptr;
    }
    GdiplusShutdown(gdiplusToken);
    
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
    }
}

// ============================================================================
// LICENSE DIALOG WINDOW PROCEDURE
// Handles OK/Cancel and Escape key to close
// ============================================================================
static LRESULT CALLBACK LicenseDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                PostQuitMessage(0);
                return 0;
            }
            break;
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                PostQuitMessage(0);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// LICENSE DIALOG
// Displays GPLv2 license text with formatting
// Loads from GPLv2.md file and applies syntax highlighting
// ============================================================================
void ShowLicenseDialog(HWND parent) {
    LoadLibraryW(L"Riched20.dll");
    
    const int W = 650, H = 600;
    RECT pr = {0,0,0,0};
    if (parent && IsWindow(parent)) GetWindowRect(parent, &pr);
    int px = (pr.right + pr.left) / 2;
    int py = (pr.bottom + pr.top) / 2;
    int x = (px==0) ? CW_USEDEFAULT : (px - W/2);
    int y = (py==0) ? CW_USEDEFAULT : (py - H/2);

    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = LicenseDlgProc;
    wc.hInstance = hi;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WPMLicenseDlgClass";
    if (!GetClassInfoW(hi, wc.lpszClassName, &wc)) {
        RegisterClassW(&wc);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE, 
        wc.lpszClassName, L"GNU General Public License v2", 
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        x, y, W, H, parent, NULL, hi, NULL);
    
    if (!dlg) {
        MessageBoxW(parent, L"Unable to create License window.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    
    // Set app icon
    HICON hIcon = LoadIcon(hi, MAKEINTRESOURCE(IDI_APP_ICON));
    if (hIcon) {
        SendMessageW(dlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(dlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }

    // Load and display GNU logo at top
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    
    // Try to load GNU logo
    std::wstring logoPath = exeDir + L"\\GnuLogo.bmp";
    HBITMAP hLogoBitmap = (HBITMAP)LoadImageW(NULL, logoPath.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    int logoHeight = 0;
    
    if (hLogoBitmap) {
        BITMAP bmp;
        GetObject(hLogoBitmap, sizeof(BITMAP), &bmp);
        logoHeight = bmp.bmHeight + 10; // Add some padding
        
        HWND hLogoWnd = CreateWindowExW(0, L"Static", NULL,
            WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            (W - bmp.bmWidth) / 2, 10, bmp.bmWidth, bmp.bmHeight,
            dlg, (HMENU)101, hi, NULL);
        SendMessageW(hLogoWnd, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hLogoBitmap);
    }

    // Create RichEdit control for license text - positioned below logo
    int editTop = logoHeight > 0 ? logoHeight + 20 : 10;
    HWND hEdit = CreateWindowExW(0, L"RichEdit20W", NULL,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        10, editTop, W - 20, H - editTop - 70,
        dlg, (HMENU)100, hi, NULL);
    
    if (!hEdit) {
        DestroyWindow(dlg);
        return;
    }
    
    SendMessageW(hEdit, EM_SETTARGETDEVICE, 0, 0); // Enable word wrap
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_LINK);
    
    // Load and parse GPLv2.md file
    std::wstring licensePath = exeDir + L"\\GPLv2.md";
    HANDLE hFile = CreateFileW(licensePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize > 0 && fileSize < 1024*1024) {
            char* buffer = new char[fileSize + 1];
            DWORD bytesRead;
            if (ReadFile(hFile, buffer, fileSize, &bytesRead, NULL)) {
                buffer[bytesRead] = '\0';
                
                int wideSize = MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, NULL, 0);
                wchar_t* wideBuffer = new wchar_t[wideSize + 1];
                MultiByteToWideChar(CP_UTF8, 0, buffer, bytesRead, wideBuffer, wideSize);
                wideBuffer[wideSize] = L'\0';
                
                std::wstring text(wideBuffer);
                delete[] wideBuffer;
                
                AppendRichText(hEdit, L"GNU GENERAL PUBLIC LICENSE\r\n", true, RGB(0, 70, 140), 14, true);
                AppendRichText(hEdit, L"Version 2, June 1991\r\n\r\n", false, RGB(0, 70, 140), 10, true);
                
                std::wstringstream ss(text);
                std::wstring line;
                
                while (std::getline(ss, line)) {
                    if (!line.empty() && line.back() == L'\r') line.pop_back();
                    
                    if (line.find(L"TERMS AND CONDITIONS") != std::wstring::npos || 
                        line.find(L"NO WARRANTY") != std::wstring::npos) {
                        AppendRichText(hEdit, L"\r\n" + line + L"\r\n", true, RGB(139, 0, 0), 11, false);
                    } else if (line == L"Preamble") {
                        AppendRichText(hEdit, L"\r\n" + line + L"\r\n", true, RGB(0, 70, 140), 12, false);
                    } else {
                        AppendRichText(hEdit, line + L"\r\n", false, RGB(40, 40, 40), 9, false);
                    }
                }
            }
            delete[] buffer;
        }
        CloseHandle(hFile);
    } else {
        AppendRichText(hEdit, L"GPLv2.md file not found.\r\n\r\n", true, RGB(139, 0, 0), 10, false);
        AppendRichText(hEdit, L"Please see the GPLv2.md file in the installation directory.", false, RGB(40, 40, 40), 9, false);
    }
    
    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    
    int btnWidth = 80;
    int btnX = (W - btnWidth) / 2;
    int btnY = H - 61;
    
    HWND btnOK = CreateWindowExW(0, L"Button", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        btnX, btnY, btnWidth, 30,
        dlg, (HMENU)IDOK, hi, NULL);
    
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessageW(btnOK, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    if (parent && IsWindow(parent)) EnableWindow(parent, FALSE);
    SetFocus(btnOK);
    
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            break;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    DestroyWindow(dlg);
    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        BringWindowToTop(parent);
    }
}

// End of About Dialog Implementation
// ============================================================================
