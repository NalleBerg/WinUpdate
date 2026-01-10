#include "install_dialog.h"
#include "Config.h"
#include "parsing.h"
#include <commctrl.h>
#include <shellapi.h>
#include <richedit.h>
#include <thread>
#include <mutex>
#include <regex>
#include <sstream>
#include <fstream>
#include <functional>

#pragma comment(lib, "comctl32.lib")

// Translation function pointer
static std::function<std::wstring(const char*)> g_translate = nullptr;

// Animation state
static int g_animFrame = 0;
static HWND g_hInstallAnim = NULL;
static std::wstring g_doneButtonText = L"Done!";
static bool g_inImportantBlock = false;  // Track if we're inside a multi-line important message
static bool g_skipNextDelimiter = false;  // Track if next delimiter should be skipped

// Install log accumulator (plain text)
static std::string g_installLog;
// RTF log accumulator (with formatting)
static std::string g_rtfLog;
static std::thread* g_installThread = nullptr;
static std::mutex g_logMutex;

static std::wstring t(const char* key) {
    if (g_translate) return g_translate(key);
    return std::wstring(key, key + strlen(key));
}

// Helper to escape RTF special characters
static std::string EscapeRtf(const std::wstring& text) {
    std::string result;
    for (wchar_t ch : text) {
        if (ch == '\\') result += "\\\\";
        else if (ch == '{') result += "\\{";
        else if (ch == '}') result += "\\}";
        else if (ch == '\r') continue;  // Skip \r
        else if (ch == '\n') result += "\\par\n";
        else if (ch < 128) result += (char)ch;
        else {
            // Unicode escape
            char buf[16];
            sprintf(buf, "\\u%d?", (int)ch);
            result += buf;
        }
    }
    return result;
}

// Helper to add text to install log (both plain text and RTF)
static void AddToLog(const std::wstring& text, bool isBold = false, COLORREF color = RGB(0, 0, 0)) {
    if (text.empty()) return;
    
    // Convert wide string to UTF-8 for plain text log
    int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
    if (size > 0) {
        std::string utf8(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &utf8[0], size, NULL, NULL);
        
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_installLog += utf8;
        
        // Add to RTF log with formatting
        // Color table: 1=Black, 2=Red, 3=Green, 4=DarkBlue, 5=BrightBlue
        int colorIndex = 1;  // Default black (color table index)
        if (color == RGB(255, 0, 0)) colorIndex = 2;  // Red
        else if (color == RGB(0, 128, 0)) colorIndex = 3;  // Green
        else if (color == RGB(0, 51, 153)) colorIndex = 4;  // Dark blue (for info/skipped)
        else if (color == RGB(0, 120, 215)) colorIndex = 5;  // Bright blue (for warnings/recommendations)
        
        if (isBold) g_rtfLog += "\\b ";
        g_rtfLog += "\\cf" + std::to_string(colorIndex) + " ";
        g_rtfLog += EscapeRtf(text);
        if (isBold) g_rtfLog += "\\b0 ";
    }
}

// Animation subclass procedure (draws moving dot overlay on progress bar)
static LRESULT CALLBACK AnimSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (uMsg == WM_TIMER && wParam == 0xBEEF) {
        g_animFrame++;
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; 
        GetClientRect(hwnd, &rc);
        
        // Clear the entire area with parent background color
        HBRUSH hBgBrush = (HBRUSH)GetClassLongPtr(GetParent(hwnd), GCLP_HBRBACKGROUND);
        if (!hBgBrush) hBgBrush = GetSysColorBrush(COLOR_WINDOW);
        FillRect(hdc, &rc, hBgBrush);
        
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        
        // Size of the dot (almost full height)
        int dotSize = height - 6;  // 3px margin on each side
        
        // Movement range (dot travels from left edge to right edge)
        int travelDistance = width - dotSize;
        
        // Bounce back and forth: double the cycle length
        int cycleLength = travelDistance * 2;
        int posInCycle = (g_animFrame * 1) % cycleLength;
        
        int position;
        if (posInCycle <= travelDistance) {
            // Moving forward
            position = posInCycle;
        } else {
            // Moving backward
            position = travelDistance - (posInCycle - travelDistance);
        }
        
        // Draw the round dot
        HBRUSH hBrush = CreateSolidBrush(RGB(0, 120, 215));
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        
        int centerX = position + dotSize / 2;
        int centerY = height / 2;
        int radius = dotSize / 2;
        
        Ellipse(hdc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
        
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (uMsg == WM_ERASEBKGND) {
        // Let WM_PAINT handle all drawing
        return 1;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// UTF-8 to Wide string conversion
static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (needed <= 0) return L"";
    std::wstring wide(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], needed);
    return wide;
}

static std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return "";
    std::string utf8(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &utf8[0], needed, NULL, NULL);
    return utf8;
}

// Helper to check if RichEdit is scrolled to the bottom
static bool IsScrolledToBottom(HWND hRichEdit) {
    // Get the first visible line and total line count
    int firstVisible = (int)SendMessageW(hRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    int totalLines = (int)SendMessageW(hRichEdit, EM_GETLINECOUNT, 0, 0);
    
    // Get the number of visible lines in the window
    RECT rect;
    GetClientRect(hRichEdit, &rect);
    
    // Use EM_GETRECT to get the formatting rectangle
    RECT formatRect;
    SendMessageW(hRichEdit, EM_GETRECT, 0, (LPARAM)&formatRect);
    
    // Calculate visible lines more accurately
    TEXTMETRICW tm;
    HDC hdc = GetDC(hRichEdit);
    GetTextMetricsW(hdc, &tm);
    ReleaseDC(hRichEdit, hdc);
    
    int lineHeight = tm.tmHeight + tm.tmExternalLeading;
    int visibleLines = (rect.bottom - rect.top) / lineHeight;
    
    // Consider at bottom if we're showing the last few lines
    return (firstVisible + visibleLines) >= (totalLines - 1);
}

// RTF streaming callback for incremental display
static DWORD CALLBACK EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
    std::string* pStr = reinterpret_cast<std::string*>(dwCookie);
    if (pStr && !pStr->empty()) {
        LONG bytesToCopy = std::min(cb, (LONG)pStr->size());
        memcpy(pbBuff, pStr->data(), bytesToCopy);
        pStr->erase(0, bytesToCopy);
        *pcb = bytesToCopy;
        return 0;
    }
    *pcb = 0;
    return 0;
}

// Global to track the RTF content displayed so far (just the inner content, not full document)
static std::string g_displayedRtfContent;
static bool g_isFirstRtfAppend = true;

// Helper: Translate error summary section text
static std::wstring TranslateSummaryLine(const std::wstring& line) {
    std::wstring result = line;
    
    // Translate section labels
    if (line.find(L"Errors:") != std::wstring::npos) {
        result = t("error_summary_title");
        if (line.length() > 7) result += line.substr(7);  // Keep any text after "Errors:"
        return result;
    }
    if (line.find(L"Error number:") != std::wstring::npos) {
        size_t pos = line.find(L"Error number:");
        result = t("error_summary_number") + line.substr(pos + 13);
        return result;
    }
    if (line.find(L"Reason:") != std::wstring::npos && line.find(L"Recommendation:") == std::wstring::npos) {
        size_t pos = line.find(L"Reason:");
        std::wstring reasonText = line.substr(pos + 7);
        // Trim leading space
        if (!reasonText.empty() && reasonText[0] == L' ') reasonText = reasonText.substr(1);
        
        // Translate specific reason texts
        if (reasonText.find(L"No applicable upgrade") != std::wstring::npos) {
            result = t("error_summary_reason") + L" " + t("error_not_applicable");
        } else if (reasonText.find(L"Installation cancelled") != std::wstring::npos) {
            result = t("error_summary_reason") + L" " + t("error_cancelled");
        } else if (reasonText.find(L"Download failed") != std::wstring::npos) {
            result = t("error_summary_reason") + L" " + t("error_download_failed");
        } else if (reasonText.find(L"Package not found") != std::wstring::npos) {
            result = t("error_summary_reason") + L" " + t("error_not_found");
        } else if (reasonText.find(L"Installation timed out") != std::wstring::npos) {
            result = t("error_summary_reason") + L" " + t("error_timeout");
        } else if (reasonText.find(L"Installation failed with unknown") != std::wstring::npos) {
            result = t("error_summary_reason") + L" " + t("error_unknown");
        } else {
            result = t("error_summary_reason") + L" " + reasonText;
        }
        return result;
    }
    if (line.find(L"Recommendation:") != std::wstring::npos) {
        size_t pos = line.find(L"Recommendation:");
        std::wstring recText = line.substr(pos + 15);
        // Trim leading space
        if (!recText.empty() && recText[0] == L' ') recText = recText.substr(1);
        
        // Translate specific recommendation texts
        if (recText.find(L"Skip this package and wait") != std::wstring::npos) {
            result = t("error_summary_recommendation") + L" " + t("recommend_skip");
        } else if (recText.find(L"Exclude this package if you want to stop") != std::wstring::npos) {
            result = t("error_summary_recommendation") + L" " + t("recommend_exclude");
        } else if (recText.find(L"Retry the installation when network") != std::wstring::npos) {
            result = t("error_summary_recommendation") + L" " + t("recommend_retry");
        } else if (recText.find(L"Exclude this package - it's no longer") != std::wstring::npos) {
            result = t("error_summary_recommendation") + L" " + t("recommend_exclude_unavailable");
        } else if (recText.find(L"Check Task Manager") != std::wstring::npos) {
            result = t("error_summary_recommendation") + L" " + t("recommend_check_taskmanager");
        } else if (recText.find(L"Search online") != std::wstring::npos) {
            result = t("error_summary_recommendation") + L" " + t("recommend_search_online");
        } else {
            result = t("error_summary_recommendation") + L" " + recText;
        }
        return result;
    }
    
    return result;
}

// Helper to append formatted text to RichEdit control using RTF streaming
static void AppendFormattedText(HWND hRichEdit, const std::wstring& text, bool isBold, COLORREF color) {
    // Translate error summary text before logging
    std::wstring translatedText = TranslateSummaryLine(text);
    
    // Add to install log (builds both plain text and RTF)
    AddToLog(translatedText, isBold, color);
    
    std::string newRtfContent;
    
    // Lock g_rtfLog access and get the new content
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        
        // Check if there's new content to display
        if (g_rtfLog.length() > g_displayedRtfContent.length()) {
            // Get only the NEW RTF content that was added
            newRtfContent = g_rtfLog.substr(g_displayedRtfContent.length());
            g_displayedRtfContent = g_rtfLog;
        }
    }
    
    // If there's new content, stream it
    if (!newRtfContent.empty()) {
        // Build RTF fragment for the new content
        std::string rtfFragment;
        
        if (g_isFirstRtfAppend) {
            // First append: include full RTF header with color table
            rtfFragment = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Consolas;}}"
                         "{\\colortbl;\\red0\\green0\\blue0;\\red255\\green0\\blue0;\\red0\\green128\\blue0;\\red0\\green51\\blue153;\\red0\\green120\\blue215;}"
                         "\\f0\\fs20 " + newRtfContent + "}";
            
            // Stream to replace all content (first time)
            EDITSTREAM es = {};
            es.dwCookie = (DWORD_PTR)&rtfFragment;
            es.pfnCallback = EditStreamCallback;
            SendMessageW(hRichEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
            
            g_isFirstRtfAppend = false;
        } else {
            // Subsequent appends: just the new content (already has proper RTF codes from AddToLog)
            rtfFragment = "{\\rtf1\\ansi{\\fonttbl{\\f0 Consolas;}}"
                         "{\\colortbl;\\red0\\green0\\blue0;\\red255\\green0\\blue0;\\red0\\green128\\blue0;\\red0\\green51\\blue153;\\red0\\green120\\blue215;}"
                         "\\f0\\fs20 " + newRtfContent + "}";
            
            // Move to end and append
            int len = GetWindowTextLengthW(hRichEdit);
            SendMessageW(hRichEdit, EM_SETSEL, len, len);
            
            // Stream to append at selection (SFF_SELECTION flag)
            EDITSTREAM es = {};
            es.dwCookie = (DWORD_PTR)&rtfFragment;
            es.pfnCallback = EditStreamCallback;
            SendMessageW(hRichEdit, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
        }
        
        // ALWAYS scroll to bottom (tail -f behavior)
        int lineCount = (int)SendMessageW(hRichEdit, EM_GETLINECOUNT, 0, 0);
        SendMessageW(hRichEdit, EM_LINESCROLL, 0, lineCount);
        SendMessageW(hRichEdit, EM_SCROLLCARET, 0, 0);
    }
}

// Helper: Check if a line contains important keywords
static bool HasImportantKeywords(const std::string& line) {
    std::string lower = line;
    for (auto& c : lower) c = tolower(c);
    
    return (lower.find("error") != std::string::npos ||
            lower.find("failed") != std::string::npos ||
            lower.find("warning") != std::string::npos ||
            lower.find("no applicable upgrade") != std::string::npos ||
            lower.find("successfully installed") != std::string::npos ||
            lower.find("installation complete") != std::string::npos ||
            lower.find("===") != std::string::npos);
}

// Helper: Check if line is only asterisks (block delimiter)
static bool IsAsteriskDelimiter(const std::string& line) {
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) trimmed.erase(0, 1);
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n')) trimmed.pop_back();
    
    // Check if line is primarily asterisks (at least 4 asterisks and mostly asterisks)
    if (trimmed.empty() || trimmed.length() < 4) return false;
    
    int asteriskCount = 0;
    for (char c : trimmed) {
        if (c == '*') asteriskCount++;
    }
    
    // If more than 50% asterisks and has at least 4, consider it a delimiter
    return asteriskCount >= 4 && (asteriskCount * 2 >= (int)trimmed.length());
}

// Check if line should be displayed in bold
// Returns: 0 = don't show, 1 = gray, 2 = bold
static int GetLineFormatting(const std::string& line, bool& inImportantBlock, bool& skipNextDelimiter) {
    bool isDelimiter = IsAsteriskDelimiter(line);
    bool hasKeywords = HasImportantKeywords(line);
    
    // If this line has important keywords, start an important block
    if (hasKeywords && !isDelimiter) {
        inImportantBlock = true;
        return 2;  // Bold
    }
    
    // Handle asterisk delimiters - hide them but toggle block state
    if (isDelimiter) {
        if (skipNextDelimiter) {
            skipNextDelimiter = false;
            return 0;  // Don't show
        }
        
        // Toggle block state but don't display the delimiter
        inImportantBlock = !inImportantBlock;
        return 0;  // Don't show asterisks
    }
    
    // Regular line - show based on block state
    if (inImportantBlock) {
        return 2;  // Bold (we're inside an important block)
    }
    
    return 1;  // Gray (normal output)
}

// Helper to determine color and boldness for a line based on its content
static void GetLineStyle(const std::wstring& line, bool& isBold, COLORREF& color) {
    isBold = false;
    color = RGB(0, 0, 0);  // Default black
    
    // Check for status indicators
    if (line.find(L"✓ Success") != std::wstring::npos) {
        color = RGB(0, 128, 0);  // Green
        isBold = true;
    }
    else if (line.find(L"ℹ️") != std::wstring::npos || line.find(L"Skipped") != std::wstring::npos) {
        color = RGB(0, 51, 153);  // Dark blue (info/skipped)
        isBold = true;
    }
    else if (line.find(L"⚠️") != std::wstring::npos || line.find(L"Cancelled") != std::wstring::npos) {
        color = RGB(0, 120, 215);  // Bright blue (warnings)
        isBold = true;
    }
    else if (line.find(L"❌") != std::wstring::npos || line.find(L"Failed") != std::wstring::npos) {
        color = RGB(255, 0, 0);  // Red
        isBold = true;
    }
    // Error summary section (bold red for title)
    else if (line.find(L"Errors:") != std::wstring::npos && line.length() < 10) {
        color = RGB(255, 0, 0);  // Red
        isBold = true;
    }
    // Error details (app name with " --" or " -- " format and ending with colon)
    else if ((line.find(L" --") != std::wstring::npos || line.find(L" -- ") != std::wstring::npos) && 
             line.back() == L':' &&
             line.find(L"Error number:") == std::wstring::npos &&
             line.find(L"Reason:") == std::wstring::npos &&
             line.find(L"Recommendation:") == std::wstring::npos) {
        color = RGB(34, 177, 76);  // Bright green (visible but not too bright)
        isBold = true;
    }
    // Recommendation lines (starts with 💡 or contains "Recommendation:")
    else if (line.find(L"💡") != std::wstring::npos || line.find(L"Recommendation:") != std::wstring::npos) {
        color = RGB(0, 120, 215);  // Bright blue (recommendations)
        isBold = false;
    }
    // Separator lines
    else if (line.find(L"═══") != std::wstring::npos || line.find(L"━━━") != std::wstring::npos) {
        color = RGB(128, 128, 128);  // Gray
    }
}

// Dialog window procedure
static LRESULT CALLBACK InstallDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hProg = NULL;
    static HWND hOut = NULL;
    static HWND hStatus = NULL;
    static HWND hAnim = NULL;
    static HWND hDone = NULL;
    static HWND hOverallStatus = NULL;
    
    switch (uMsg) {
    case WM_CREATE: {
        HINSTANCE hInst = GetModuleHandleW(NULL);
        INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_PROGRESS_CLASS };
        InitCommonControlsEx(&icce);
        
        const int W = 640, H = 480;
        
        // Overall progress label (centered)
        hOverallStatus = CreateWindowExW(0, L"Static", t("install_preparing").c_str(), WS_CHILD | WS_VISIBLE | SS_CENTER, 
            20, 20, W-40, 24, hwnd, (HMENU)1002, hInst, NULL);
        HFONT hFontBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        SendMessageW(hOverallStatus, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        
        // Status label (package-specific, above progress bar)
        HFONT hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        hStatus = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            24, 48, W-48, 20, hwnd, NULL, hInst, NULL);
        SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // Progress bar (always visible, segmented style)
        hProg = CreateWindowExW(0, PROGRESS_CLASSW, NULL, 
            WS_CHILD | WS_VISIBLE, 
            20, 72, W-40, 24, hwnd, (HMENU)2001, hInst, NULL);
        SendMessageW(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(hProg, PBM_SETPOS, 10, 0);  // Set initial position to make it visible
        ShowWindow(hProg, SW_SHOW);  // Explicitly show the progress bar
        // Ensure progress bar is on top
        SetWindowPos(hProg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        UpdateWindow(hProg);
        InvalidateRect(hProg, NULL, TRUE);
        
        // Force parent window update to ensure controls are visible
        UpdateWindow(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        
        // Animation overlay (hidden initially, shown during install phase)
        hAnim = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", NULL, WS_CHILD, 
            20, 72, W-40, 24, hwnd, (HMENU)2002, hInst, NULL);
        g_hInstallAnim = hAnim;
        SetWindowSubclass(hAnim, AnimSubclassProc, 0, 0);
        SetTimer(hAnim, 0xBEEF, 1, NULL); // 1ms refresh rate
        
        // Output edit (larger area) - using RichEdit for selective formatting
        LoadLibraryW(L"Riched20.dll");
        hOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"RichEdit20W", NULL, 
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL | ES_WANTRETURN,
            20, 110, W-40, H-190, hwnd, NULL, hInst, NULL);
        
        // Enable RTF mode for proper RTF generation
        SendMessageW(hOut, EM_SETTEXTMODE, TM_RICHTEXT, 0);
        
        // Enable word wrap
        SendMessageW(hOut, EM_SETTARGETDEVICE, 0, 0);
        
        // Set default font
        HFONT hEditFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Consolas");
        SendMessageW(hOut, WM_SETFONT, (WPARAM)hEditFont, TRUE);
        SendMessageW(hOut, EM_SETBKGNDCOLOR, 0, (LPARAM)GetSysColor(COLOR_WINDOW));
        
        // Initialize second status line with placeholder to ensure it's visible
        SetWindowTextW(hStatus, L"Klargjør...");
        ShowWindow(hStatus, SW_SHOW);
        UpdateWindow(hStatus);
        
        // Done button (centered, disabled initially)
        hDone = CreateWindowExW(0, L"Button", g_doneButtonText.c_str(), WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON, 
            (W-150)/2, H-70, 150, 32, hwnd, (HMENU)1001, hInst, NULL);
        
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hStatic = (HWND)lParam;
        // Make static controls above progress bar have white background
        if (hStatic == hOverallStatus || hStatic == hStatus) {
            SetBkColor(hdcStatic, RGB(255, 255, 255));
            return (INT_PTR)GetStockObject(WHITE_BRUSH);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) {
            // Done button clicked - bring parent window to front then destroy this dialog
            HWND hParent = GetParent(hwnd);
            DestroyWindow(hwnd);
            if (hParent && IsWindow(hParent)) {
                ShowWindow(hParent, SW_RESTORE);
                SetForegroundWindow(hParent);
                SetWindowPos(hParent, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetWindowPos(hParent, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                SetActiveWindow(hParent);
                SetFocus(hParent);
            }
        }
        return 0;
    case WM_CLOSE:
        // Allow closing after install completes (when Done button is enabled)
        if (hDone && IsWindowEnabled(hDone)) {
            DestroyWindow(hwnd);
        } else {
            // Show cancelling message and terminate helper process
            if (g_installThread && g_installThread->joinable()) {
                MessageBoxW(hwnd, L"Avbryter installasjonen...\n\nVennligst vent mens prosessen avsluttes.",
                           L"Avbryter", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                // The thread will detect process termination and clean up
            }
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        if (hAnim) {
            KillTimer(hAnim, 0xBEEF);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Parse winget output line-by-line and detect phases
static void ProcessWingetOutput(const std::string& line, HWND hwnd, HWND hProg, HWND hAnim, HWND hStatus, HWND hOut,
                                 std::string& currentPhase, std::string& currentPackage) {
    // Trim whitespace
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r'))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n'))
        trimmed.pop_back();
    
    if (trimmed.empty()) return;
    
    // Detect phase changes
    if (trimmed.find("Found ") == 0) {
        // Extract package name
        size_t start = 6; // "Found "
        size_t bracket = trimmed.find('[', start);
        if (bracket != std::string::npos) {
            currentPackage = trimmed.substr(start, bracket - start);
            // Trim trailing space
            while (!currentPackage.empty() && currentPackage.back() == ' ')
                currentPackage.pop_back();
        }
    }
    else if (trimmed.find("Downloading") == 0 || trimmed.find("Download started") != std::string::npos) {
        currentPhase = "download";
        // Show progress bar, hide animation
        ShowWindow(hProg, SW_SHOW);
        ShowWindow(hAnim, SW_HIDE);
        
        // Use translation key with package name
        if (!currentPackage.empty()) {
            wchar_t pkgBuf[512];
            std::wstring pkgWide = Utf8ToWide(currentPackage);
            swprintf(pkgBuf, 512, t("downloading_package").c_str(), pkgWide.c_str());
            SetWindowTextW(hStatus, pkgBuf);
        } else {
            SetWindowTextW(hStatus, L"Downloading...");
        }
        
        // Reset progress bar for download (0-100%)
        SendMessageW(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(hProg, PBM_SETPOS, 0, 0);
    }
    else if (trimmed.find("Download complete") != std::string::npos || 
             trimmed.find("Installing") == 0 || 
             trimmed.find("Successfully installed") == 0) {
        if (currentPhase != "install" && trimmed.find("Installing") == 0) {
            currentPhase = "install";
            // Show animation overlay (progress bar stays visible underneath)
            ShowWindow(hAnim, SW_SHOW);
            
            // Reset animation to start position
            g_animFrame = 0;
            
            wchar_t pkgBuf[512];
            std::wstring pkgWide = Utf8ToWide(currentPackage);
            swprintf(pkgBuf, 512, t("installing_package").c_str(), pkgWide.c_str());
            std::wstring statusText = pkgBuf;
            SetWindowTextW(hStatus, statusText.c_str());
        }
    }
    
    // Parse download progress (e.g., "10 MB / 100 MB" or percentage indicators)
    if (currentPhase == "download") {
        // Try MB/MB format
        std::regex mbRe(R"((\d+)\s*(MB|MiB)\s*/\s*(\d+)\s*(MB|MiB))", std::regex::icase);
        std::smatch match;
        if (std::regex_search(trimmed, match, mbRe)) {
            try {
                int current = std::stoi(match[1].str());
                int total = std::stoi(match[3].str());
                if (total > 0) {
                    int percent = (current * 100) / total;
                    SendMessageW(hProg, PBM_SETPOS, percent, 0);
                }
            } catch (...) {}
        }
        
        // Try percentage format (e.g., "45%")
        std::regex percRe(R"((\d+)%)");
        if (std::regex_search(trimmed, match, percRe)) {
            try {
                int percent = std::stoi(match[1].str());
                if (percent >= 0 && percent <= 100) {
                    SendMessageW(hProg, PBM_SETPOS, percent, 0);
                }
            } catch (...) {}
        }
        
        // Try progress bar character patterns (e.g., "█████░░░░░")
        // Count filled vs total characters as percentage
        size_t filled = 0;
        size_t total = 0;
        for (char c : trimmed) {
            if (c == (char)0x88 || c == (char)0xDB || c == '#' || c == '=' || c == '>') { // Various progress chars
                filled++;
                total++;
            } else if (c == (char)0x91 || c == (char)0xB0 || c == (char)0xB1 || c == ' ' || c == '-' || c == '.') {
                total++;
            }
        }
        if (total > 10 && filled > 0) { // At least 10 chars to be considered a progress bar
            int percent = (filled * 100) / total;
            SendMessageW(hProg, PBM_SETPOS, percent, 0);
        }
    }
}

// Filter output lines - return true if line should be displayed
static bool ShouldDisplayLine(const std::string& line) {
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r'))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n'))
        trimmed.pop_back();
    
    if (trimmed.empty()) return false;
    
    // Skip PowerShell transcript messages (header, footer, and metadata)
    if (trimmed.find("Transcript started") != std::string::npos || 
        trimmed.find("Transcript stopped") != std::string::npos ||
        trimmed.find("output file is") != std::string::npos ||
        trimmed.find("Windows PowerShell transcript") != std::string::npos ||
        trimmed.find("Start time:") == 0 ||
        trimmed.find("End time:") == 0 ||
        trimmed.find("Username:") == 0 ||
        trimmed.find("RunAs User:") == 0 ||
        trimmed.find("Configuration Name:") == 0 ||
        trimmed.find("Machine:") == 0 ||
        trimmed.find("Host Application:") == 0 ||
        trimmed.find("Process ID:") == 0 ||
        trimmed.find("PSVersion:") == 0 ||
        trimmed.find("PSEdition:") == 0 ||
        trimmed.find("PSCompatibleVersions:") == 0 ||
        trimmed.find("BuildVersion:") == 0 ||
        trimmed.find("CLRVersion:") == 0 ||
        trimmed.find("WSManStackVersion:") == 0 ||
        trimmed.find("PSRemotingProtocolVersion:") == 0 ||
        trimmed.find("SerializationVersion:") == 0) {
        return false;
    }
    
    // Skip download progress lines (we handle them separately)
    if (trimmed.find("Downloading") == 0 && trimmed.find("MB") != std::string::npos) {
        return false;
    }
    
    // Skip progress bars with percentage (e.g., "2%  ██████████████▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒")
    static const std::regex progressBarRe(R"(^\s*\d+%\s*[█▓▒░\s]+$)");
    if (std::regex_match(trimmed, progressBarRe)) {
        return false;
    }
    
    // Skip spinner animation lines (e.g., "    -     \     |     /     -     \     |")
    // Match lines with only spaces and spinner characters (-, \, |, /) with spaces between them
    static const std::regex spinnerRe(R"(^[\s\-\\\\/]+$)");
    if (std::regex_match(trimmed, spinnerRe)) {
        return false;
    }
    
    // Also skip lines that are just repeated spinner patterns with spacing
    // E.g., "  -   \   |   /   -   \  "
    size_t spinChars = 0;
    size_t totalChars = trimmed.length();
    for (char ch : trimmed) {
        if (ch == '-' || ch == '\\' || ch == '|' || ch == '/' || ch == ' ' || ch == '\t') {
            spinChars++;
        }
    }
    if (totalChars > 5 && spinChars == totalChars) {
        return false;  // Line is only spinner characters and spaces
    }
    
    // Skip lines that are mostly progress bar characters
    static const std::regex blockBarRe(R"([█▓▒░]{8,})");
    if (std::regex_search(trimmed, blockBarRe)) {
        return false;
    }
    
    return true;
}

bool ShowInstallDialog(HWND hParent, const std::vector<std::string>& packageIds, 
                      const std::wstring& doneButtonText,
                      std::function<std::wstring(const char*)> translateFunc) {
    if (packageIds.empty()) return false;
    
    // Clear install log for new session
    g_installLog.clear();
    
    // Store translate function and done button text
    g_translate = translateFunc;
    g_doneButtonText = doneButtonText;
    
    // Reset important block state for new installation
    g_inImportantBlock = false;
    g_skipNextDelimiter = false;
    
    // Register dialog class
    const wchar_t CLASS_NAME[] = L"WinUpdateInstallDialog";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = InstallDlgProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    // Create modal dialog
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, t("install_dialog_title").c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        hParent, NULL, GetModuleHandleW(NULL), NULL);
    
    if (!hwnd) return false;
    
    // Get dialog controls
    HWND hOverallStatus = GetDlgItem(hwnd, 1002);
    HWND hStatus = NULL;
    HWND hProg = NULL;
    HWND hAnim = NULL;
    HWND hOut = NULL;
    HWND hDone = GetDlgItem(hwnd, 1001);
    
    // Find controls by iterating children
    struct ControlHandles {
        HWND* hProg;
        HWND* hOut;
        HWND* hAnim;
        HWND* hStatus;
    };
    
    ControlHandles handles{&hProg, &hOut, &hAnim, &hStatus};
    
    EnumChildWindows(hwnd, [](HWND hChild, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassNameW(hChild, className, 256);
        
        auto* handles = (ControlHandles*)lParam;
        
        if (wcscmp(className, L"msctls_progress32") == 0) {
            *handles->hProg = hChild;
        } else if (_wcsicmp(className, L"Edit") == 0 || _wcsicmp(className, L"RichEdit20W") == 0) {  // Support both Edit and RichEdit
            *handles->hOut = hChild;
        } else if (wcscmp(className, L"Static") == 0) {
            RECT rc;
            GetWindowRect(hChild, &rc);
            int height = rc.bottom - rc.top;
            // Animation overlay is about 28px high
            if (height >= 20 && height <= 35) {
                *handles->hAnim = hChild;
            } else if (height < 25 && *handles->hStatus == NULL) {
                *handles->hStatus = hChild;
            }
        }
        return TRUE;
    }, (LPARAM)&handles);
    
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    // Update overall status with correct count
    wchar_t initialBuf[256];
    swprintf(initialBuf, 256, t("install_progress").c_str(), (int)packageIds.size());
    std::wstring initialStatus = initialBuf;
    SendMessageW(hOverallStatus, WM_SETTEXT, 0, (LPARAM)initialStatus.c_str());
    
    // Start winget_helper in background thread with UAC elevation
    auto installFunc = [hwnd, hOut, hProg, hStatus, hDone, hAnim, hOverallStatus, packageIds]() {
        // Reset RTF tracking for new installation
        g_displayedRtfContent.clear();
        g_isFirstRtfAppend = true;
        
        // Start with progress bar visible (default to download mode), animation hidden
        ShowWindow(hProg, SW_SHOW);
        ShowWindow(hAnim, SW_HIDE);
        SendMessageW(hProg, PBM_SETPOS, 0, 0);
        
        // Force progress bar to front to ensure visibility
        SetWindowPos(hProg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        InvalidateRect(hProg, NULL, TRUE);
        UpdateWindow(hProg);
        
        // Track current phase and package name
        std::wstring currentPhase = L"download";  // Start in download mode by default
        std::wstring currentAppName = L"";  // Display name for status
        
        // Create a unique named pipe for IPC (in-memory communication)
        std::wstring pipeName = L"\\\\.\\pipe\\WinUpdate_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount());
        
        HANDLE hPipe = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            65536,
            65536,
            0,
            NULL
        );
        
        if (hPipe == INVALID_HANDLE_VALUE) {
            std::wstring errMsg = t("install_error_pipe") + L"\r\n";
            SendMessageW(hStatus, WM_SETTEXT, 0, (LPARAM)errMsg.c_str());
            AppendFormattedText(hOut, errMsg, true, RGB(255, 0, 0));
            EnableWindow(hDone, TRUE);
            return;
        }
        
        // Get the path to winget_helper.exe (same directory as WinUpdate.exe)
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            exeDir = exeDir.substr(0, lastSlash);
        }
        std::wstring helperPath = exeDir + L"\\winget_helper.exe";
        
        // Write package name map to temporary file for winget_helper to read
        std::wstring mapFile = exeDir + L"\\wup_pkg_names.txt";
        {
            std::lock_guard<std::mutex> lock(g_packages_mutex);
            std::wofstream ofs(mapFile.c_str());
            if (ofs) {
                for (const auto& [id, name] : g_packages) {
                    ofs << Utf8ToWide(id) << L"|" << Utf8ToWide(name) << L"\n";
                }
            }
        }
        
        // Clear winget cache before installing to force fresh downloads
        std::wstring clearCmd = L"cmd.exe /c winget source reset --force >nul 2>&1";
        STARTUPINFOW siClear = {};
        siClear.cb = sizeof(siClear);
        siClear.dwFlags = STARTF_USESHOWWINDOW;
        siClear.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION piClear = {};
        if (CreateProcessW(NULL, (LPWSTR)clearCmd.c_str(), NULL, NULL, FALSE,
                          CREATE_NO_WINDOW, NULL, NULL, &siClear, &piClear)) {
            WaitForSingleObject(piClear.hProcess, 15000);  // Wait max 15 seconds
            CloseHandle(piClear.hThread);
            CloseHandle(piClear.hProcess);
        }
        
        // Build parameters: pipe name first, then all package IDs
        std::wstring helperParams = L"\"" + pipeName + L"\"";
        for (const auto& pkgId : packageIds) {
            helperParams += L" \"" + Utf8ToWide(pkgId) + L"\"";
        }
        
        // Run winget_helper.exe elevated with UAC (single prompt)
        // It's a GUI app so no console window will appear
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.hwnd = hwnd;
        sei.lpVerb = L"runas";
        sei.lpFile = helperPath.c_str();
        sei.lpParameters = helperParams.c_str();
        sei.nShow = SW_HIDE;
        
        if (!ShellExecuteExW(&sei) || !sei.hProcess) {
            // User cancelled UAC or elevation failed
            DWORD err = GetLastError();
            std::wstring errMsg = t("install_error_elevate") + L" (" + std::to_wstring(err) + L")";
            SendMessageW(hStatus, WM_SETTEXT, 0, (LPARAM)errMsg.c_str());
            AppendFormattedText(hOut, errMsg + L"\r\n", true, RGB(255, 0, 0));
            EnableWindow(hDone, TRUE);
            CloseHandle(hPipe);
            return;
        }
        
        // Wait for helper to connect to the pipe (non-blocking with timeout)
        DWORD pipeMode = PIPE_NOWAIT;
        SetNamedPipeHandleState(hPipe, &pipeMode, NULL, NULL);
        
        bool connected = false;
        for (int i = 0; i < 50; i++) {  // Wait up to 5 seconds
            if (ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
                connected = true;
                break;
            }
            Sleep(100);
        }
        
        if (!connected) {
            AppendFormattedText(hOut, t("install_error_connect") + L"\r\n", true, RGB(255, 0, 0));
            CloseHandle(hPipe);
            CloseHandle(sei.hProcess);
            EnableWindow(hDone, TRUE);
            return;
        }
        
        // Read output from pipe progressively while process runs
        int timeoutCounter = 0;
        const int TIMEOUT_LIMIT = 600; // 5 minutes (600 * 500ms)
        int completedPackages = 0;
        std::wstring currentPackageId;
        std::wstring lineBuffer;  // Accumulate partial lines
        
        while (true) {
            DWORD waitResult = WaitForSingleObject(sei.hProcess, 100);
            
            timeoutCounter++;
            if (timeoutCounter > TIMEOUT_LIMIT) {
                // Timeout - terminate the process
                TerminateProcess(sei.hProcess, 1);
                std::wstring timeoutMsg = t("install_error_timeout") + L"\r\n";
                AppendFormattedText(hOut, timeoutMsg, true, RGB(255, 0, 0));
                break;
            }
            
            // Read from named pipe
            char buffer[4096];
            DWORD bytesRead;
            while (ReadFile(hPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                
                // Convert UTF-8 to wide string and add to buffer
                std::wstring wtext = Utf8ToWide(std::string(buffer, bytesRead));
                if (!wtext.empty()) {
                    lineBuffer += wtext;
                    
                    // Handle carriage returns (\r) - they're used by spinners to overwrite same line
                    // If we have \r without \n, it means "go back to start of line and overwrite"
                    // Keep only content after the last \r in the buffer
                    size_t lastCR = lineBuffer.find_last_of(L'\r');
                    if (lastCR != std::wstring::npos) {
                        // Check if there's a newline after this \r
                        size_t nextNL = lineBuffer.find(L'\n', lastCR);
                        if (nextNL == std::wstring::npos) {
                            // No newline after \r - this is a spinner update, keep only after last \r
                            lineBuffer = lineBuffer.substr(lastCR + 1);
                        }
                    }
                    
                    // Process complete lines
                    size_t pos = 0;
                    while ((pos = lineBuffer.find(L'\n')) != std::wstring::npos) {
                        std::wstring line = lineBuffer.substr(0, pos);
                        lineBuffer.erase(0, pos + 1);
                        
                        // Remove \r if present
                        if (!line.empty() && line.back() == L'\r') {
                            line.pop_back();
                        }
                        
                        // PARSE PROGRESS BEFORE FILTERING (so we capture MB values even if line is hidden)
                        if (currentPhase == L"download") {
                            // Look for "XX MB / YY MB" pattern
                            size_t mbPos = line.find(L" MB");
                            if (mbPos != std::wstring::npos) {
                                size_t slashPos = line.rfind(L" / ", mbPos);
                                if (slashPos != std::wstring::npos) {
                                    // Find the total MB after the slash
                                    size_t totalStart = slashPos + 3;
                                    size_t totalEnd = line.find(L" MB", totalStart);
                                    if (totalEnd != std::wstring::npos) {
                                        // Find the current MB before the slash
                                        size_t currentStart = slashPos;
                                        while (currentStart > 0 && (iswdigit(line[currentStart - 1]) || line[currentStart - 1] == L'.' || line[currentStart - 1] == L' ')) {
                                            currentStart--;
                                        }
                                        // Trim leading spaces
                                        while (currentStart < slashPos && line[currentStart] == L' ') {
                                            currentStart++;
                                        }
                                        try {
                                            double currentMB = std::stod(line.substr(currentStart, slashPos - currentStart));
                                            double totalMB = std::stod(line.substr(totalStart, totalEnd - totalStart));
                                            if (totalMB > 0) {
                                                int percent = (int)((currentMB / totalMB) * 100.0);
                                                if (percent >= 0 && percent <= 100) {
                                                    SendMessageW(hProg, PBM_SETPOS, percent, 0);
                                                    InvalidateRect(hProg, NULL, TRUE);
                                                    UpdateWindow(hProg);
                                                }
                                            }
                                        } catch (...) {}
                                    }
                                }
                            }
                        }
                        
                        // Filter and display
                        std::string narrowLine = WideToUtf8(line + L"\n");
                        if (ShouldDisplayLine(narrowLine)) {
                            // Trim leading and trailing whitespace from the line before display
                            std::wstring trimmedLine = line;
                            while (!trimmedLine.empty() && iswspace(trimmedLine.front())) {
                                trimmedLine.erase(0, 1);
                            }
                            while (!trimmedLine.empty() && iswspace(trimmedLine.back())) {
                                trimmedLine.pop_back();
                            }
                            
                            // Determine color and styling based on content
                            bool isBold = false;
                            COLORREF color = RGB(0, 0, 0);
                            GetLineStyle(trimmedLine, isBold, color);
                            
                            AppendFormattedText(hOut, trimmedLine + L"\n", isBold, color);
                        }
                    }
                    
                    // If buffer is getting large without newlines (spinner updates), check and clear if it's junk
                    if (lineBuffer.length() > 200) {
                        std::string check = WideToUtf8(lineBuffer);
                        if (!ShouldDisplayLine(check)) {
                            lineBuffer.clear();  // Discard spinner/progress bar updates
                        }
                    }
                    
                    // Extract app name from "Found" lines
                    // Format: "Found App Name [Package.ID] Version X.X.X"
                    // Also: "(1/2) Found App Name [Package.ID] Version X.X.X"
                    size_t foundPos = wtext.find(L"Found ");
                    if (foundPos != std::wstring::npos) {
                        size_t nameStart = foundPos + 6;  // After "Found "
                        size_t bracketPos = wtext.find(L"[", nameStart);
                        if (bracketPos != std::wstring::npos) {
                            currentAppName = wtext.substr(nameStart, bracketPos - nameStart);
                            // Trim leading whitespace
                            while (!currentAppName.empty() && iswspace(currentAppName.front())) {
                                currentAppName.erase(0, 1);
                            }
                            // Trim trailing whitespace
                            while (!currentAppName.empty() && iswspace(currentAppName.back())) {
                                currentAppName.pop_back();
                            }
                        }
                    }
                    
                    // Detect download phase (multiple patterns)
                    if (wtext.find(L"Downloading") != std::wstring::npos || 
                        wtext.find(L"  ██") != std::wstring::npos ||  // Progress bar characters
                        wtext.find(L"Download started") != std::wstring::npos) {
                        if (currentPhase != L"download") {
                            currentPhase = L"download";
                            // Show progress bar, hide animation
                            ShowWindow(hProg, SW_SHOW);
                            ShowWindow(hAnim, SW_HIDE);
                            // Reset progress bar
                            SendMessageW(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
                            SendMessageW(hProg, PBM_SETPOS, 0, 0);
                            // Update status text
                            if (!currentAppName.empty()) {
                                wchar_t statusBuf[512];
                                swprintf(statusBuf, 512, t("downloading_package").c_str(), currentAppName.c_str());
                                SetWindowTextW(hStatus, statusBuf);
                            } else {
                                SetWindowTextW(hStatus, L"Downloading...");
                            }
                            // Force immediate update
                            UpdateWindow(hProg);
                            UpdateWindow(hStatus);
                            InvalidateRect(hProg, NULL, TRUE);
                            InvalidateRect(hStatus, NULL, TRUE);
                        }
                    }
                    
                    // Detect install phase
                    if (wtext.find(L"Starting package install") != std::wstring::npos ||
                        (wtext.find(L"Installing") != std::wstring::npos && wtext.find(L"Successfully installed") == std::wstring::npos)) {
                        if (currentPhase != L"install") {
                            currentPhase = L"install";
                            // Show animation overlay (progress bar stays visible underneath)
                            ShowWindow(hAnim, SW_SHOW);
                            // Reset animation
                            g_animFrame = 0;
                            // Update status text
                            if (!currentAppName.empty()) {
                                wchar_t statusBuf[512];
                                swprintf(statusBuf, 512, t("installing_package").c_str(), currentAppName.c_str());
                                SetWindowTextW(hStatus, statusBuf);
                            }
                            // Force immediate update
                            UpdateWindow(hAnim);
                            UpdateWindow(hStatus);
                            InvalidateRect(hStatus, NULL, TRUE);
                        }
                    }
                    
                    // Look for success/fail markers (phase complete)
                    if (wtext.find(L"✓ Success") != std::wstring::npos || 
                        wtext.find(L"ℹ️") != std::wstring::npos || 
                        wtext.find(L"⚠️") != std::wstring::npos ||
                        wtext.find(L"❌") != std::wstring::npos) {
                        // Package completed - reset phase for next package
                        currentPhase = L"";
                        currentAppName = L"";
                        ShowWindow(hAnim, SW_HIDE);
                        SetWindowTextW(hStatus, L"");
                    }
                }
            }
            
            if (waitResult == WAIT_OBJECT_0) break;
        }
        
        CloseHandle(hPipe);
        CloseHandle(sei.hProcess);
        
        // Hide animation, show completion
        ShowWindow(hAnim, SW_HIDE);
        SendMessageW(hProg, PBM_SETPOS, 100, 0);
        SetWindowTextW(hStatus, t("install_complete").c_str());
        EnableWindow(hDone, TRUE);
        
        // Update overall status to show completion
        wchar_t completeBuf[256];
        // Status already shows total packages, no need to update on completion
        
        // Append completion message in bold
        std::wstring completionMsg = L"\r\n\r\n=== Installation Complete ===\r\n";
        completionMsg += std::to_wstring(packageIds.size()) + L" package(s) processed.\r\n";
        AppendFormattedText(hOut, completionMsg, true, RGB(0, 0, 0));
        
        // Force window update and scroll aggressively to ensure text is visible
        int finalLineCount = (int)SendMessageW(hOut, EM_GETLINECOUNT, 0, 0);
        for (int i = 0; i < 3; i++) {  // Multiple scroll attempts
            SendMessageW(hOut, EM_LINESCROLL, 0, finalLineCount);
            SendMessageW(hOut, WM_VSCROLL, SB_BOTTOM, 0);
            SendMessageW(hOut, EM_SCROLLCARET, 0, 0);
        }
        InvalidateRect(hOut, NULL, TRUE);
        UpdateWindow(hOut);
        RedrawWindow(hOut, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    };
    
    // Store thread (don't detach - we need to join it before saving log)
    g_installThread = new std::thread(installFunc);
    
    // Modal message loop
    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)) {
            if (msg.message == WM_QUIT) break;
            if (!IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        // Check if dialog was closed
        if (!IsWindow(hwnd)) break;
    }
    
    DestroyWindow(hwnd);
    UnregisterClassW(CLASS_NAME, GetModuleHandleW(NULL));
    
    // Wait for install thread to complete before saving log
    if (g_installThread) {
        if (g_installThread->joinable()) {
            g_installThread->join();
        }
        delete g_installThread;
        g_installThread = nullptr;
    }
    
    // Save install log to INI file (always save, even if empty, for debugging)
    // Build complete RTF document with header and color table
    std::string completeRtf = 
        "{\\rtf1\\ansi\\deff0"
        "{\\fonttbl{\\f0 Consolas;}}"
        "{\\colortbl;\\red0\\green0\\blue0;\\red255\\green0\\blue0;\\red0\\green128\\blue0;\\red0\\green51\\blue153;\\red0\\green120\\blue215;}"
        "\\f0\\fs20 ";
    
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        completeRtf += g_rtfLog;
        g_installLog.clear();
        g_rtfLog.clear();
    }
    
    completeRtf += "}";
    
    // Debug: check RTF format
    std::ofstream debugFile("C:\\Users\\NalleBerg\\Documents\\C++\\Workspace\\WinUpdate\\rtf_debug.txt");
    if (debugFile) {
        debugFile << "RTF Log size: " << completeRtf.size() << " bytes\n";
        debugFile << "First 200 chars: " << completeRtf.substr(0, std::min(size_t(200), completeRtf.size())) << "\n";
        debugFile << "Is RTF format: " << (completeRtf.find("{\\rtf") == 0 ? "YES" : "NO") << "\n";
        debugFile.close();
    }
    
    SaveInstallLog(completeRtf);
    
    return true;
}
