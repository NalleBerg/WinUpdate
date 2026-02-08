#include "quit_handler.h"
#include "resource.h"
#include <commctrl.h>

// Global for hover tracking
static HWND g_hQuitHoverButton = NULL;
static WNDPROC g_oldQuitButtonProc = NULL;

// Button subclass for hover effects
static LRESULT CALLBACK QuitButtonSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_MOUSEMOVE: {
            if (g_hQuitHoverButton != hwnd) {
                g_hQuitHoverButton = hwnd;
                InvalidateRect(hwnd, NULL, FALSE);
                
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = HOVER_DEFAULT;
                TrackMouseEvent(&tme);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (g_hQuitHoverButton == hwnd) {
                g_hQuitHoverButton = NULL;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
    }
    return CallWindowProc(g_oldQuitButtonProc, hwnd, uMsg, wParam, lParam);
}

// Custom dialog for quit confirmation with blue owner-drawn buttons
INT_PTR CALLBACK QuitDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static const std::wstring* pYesBtn = nullptr;
    static const std::wstring* pNoBtn = nullptr;
    
    switch (message) {
        case WM_INITDIALOG: {
            // Get strings passed via lParam
            auto* strings = reinterpret_cast<std::wstring**>(lParam);
            pYesBtn = strings[1];
            pNoBtn = strings[2];
            
            // Center on parent
            HWND hParent = GetParent(hDlg);
            if (hParent) {
                RECT rcParent, rcDlg;
                GetWindowRect(hParent, &rcParent);
                GetWindowRect(hDlg, &rcDlg);
                int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
                int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
                SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            
            // Set message text and button texts
            SetDlgItemTextW(hDlg, IDC_QUIT_MESSAGE, strings[0]->c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDOK), pYesBtn->c_str());
            SetWindowTextW(GetDlgItem(hDlg, IDCANCEL), pNoBtn->c_str());
            
            // Subclass buttons for hover effects
            HWND hYes = GetDlgItem(hDlg, IDOK);
            HWND hNo = GetDlgItem(hDlg, IDCANCEL);
            if (hYes && !g_oldQuitButtonProc) {
                g_oldQuitButtonProc = (WNDPROC)SetWindowLongPtr(hYes, GWLP_WNDPROC, (LONG_PTR)QuitButtonSubclassProc);
            }
            if (hNo && g_oldQuitButtonProc) {
                SetWindowLongPtr(hNo, GWLP_WNDPROC, (LONG_PTR)QuitButtonSubclassProc);
            }
            
            return TRUE;
        }
        
        case WM_CTLCOLORDLG: {
            return (INT_PTR)GetStockObject(WHITE_BRUSH);
        }
        
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(0, 0, 0));
            SetBkColor(hdcStatic, RGB(255, 255, 255));
            return (INT_PTR)GetStockObject(WHITE_BRUSH);
        }
        
        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
            
            // Draw question mark icon using system icon
            if (pDIS->CtlID == IDC_QUIT_ICON) {
                HDC hdc = pDIS->hDC;
                RECT rc = pDIS->rcItem;
                
                // Fill background
                HBRUSH hBrushBg = (HBRUSH)GetStockObject(WHITE_BRUSH);
                FillRect(hdc, &rc, hBrushBg);
                
                // Load and draw system question mark icon
                HICON hIcon = LoadIcon(NULL, IDI_QUESTION);
                if (hIcon) {
                    DrawIconEx(hdc, rc.left, rc.top, hIcon, 32, 32, 0, NULL, DI_NORMAL);
                }
                
                return TRUE;
            }
            
            if (pDIS->CtlType == ODT_BUTTON) {
                HDC hdc = pDIS->hDC;
                RECT rc = pDIS->rcItem;
                
                // Blue theme matching About button RGB(10, 57, 129)
                COLORREF baseColor = RGB(10, 57, 129);
                COLORREF hoverColor = RGB(40, 87, 159);
                
                // Determine if this button is hovered
                bool isHovered = (g_hQuitHoverButton == pDIS->hwndItem);
                COLORREF btnColor = isHovered ? hoverColor : baseColor;
                
                // Fill button background
                HBRUSH hBrush = CreateSolidBrush(btnColor);
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                
                // Draw button text
                wchar_t text[50];
                GetWindowTextW(pDIS->hwndItem, text, 50);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                
                // Use bold font like main window buttons with larger size
                HFONT hFont = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                HGDIOBJ oldFont = SelectObject(hdc, hFont);
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldFont);
                DeleteObject(hFont);
                
                // Draw focus rectangle
                if (pDIS->itemState & ODS_FOCUS) {
                    RECT rcFocus = rc;
                    InflateRect(&rcFocus, -3, -3);
                    DrawFocusRect(hdc, &rcFocus);
                }
                
                return TRUE;
            }
            break;
        }
        
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                EndDialog(hDlg, IDYES);
                return TRUE;
            }
            else if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, IDNO);
                return TRUE;
            }
            break;
            
        case WM_CLOSE:
            EndDialog(hDlg, IDNO);
            return TRUE;
    }
    return FALSE;
}

bool ShowQuitConfirmation(HWND hParent, const std::wstring& /*title*/, const std::wstring& message,
                          const std::wstring& yesBtn, const std::wstring& noBtn) {
    // Pass strings to dialog via lParam as array of pointers
    std::wstring* strings[3] = {
        const_cast<std::wstring*>(&message),
        const_cast<std::wstring*>(&yesBtn),
        const_cast<std::wstring*>(&noBtn)
    };
    
    // Show custom dialog with blue owner-drawn buttons
    int result = DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_QUIT_DIALOG),
                                  hParent, QuitDialogProc, (LPARAM)strings);
    return result == IDYES;
}

bool HandleCtrlW(HWND hwnd) {
    // Check if there are any visible child dialogs
    HWND hChild = GetLastActivePopup(hwnd);
    if (hChild != hwnd && IsWindowVisible(hChild)) {
        // Close the child dialog without confirmation
        PostMessageW(hChild, WM_CLOSE, 0, 0);
        return true;
    }
    
    // No child dialogs - this would close the main window
    // Return false to let the main window handler show confirmation
    return false;
}
