#include "quit_handler.h"
#include <commctrl.h>

// Custom dialog for quit confirmation with question mark icon
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
            
            // Set message text
            SetDlgItemTextW(hDlg, IDOK, pYesBtn->c_str());
            SetDlgItemTextW(hDlg, IDCANCEL, pNoBtn->c_str());
            
            return TRUE;
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

bool ShowQuitConfirmation(HWND hParent, const std::wstring& title, const std::wstring& message,
                          const std::wstring& /*yesBtn*/, const std::wstring& /*noBtn*/) {
    // Use standard MessageBox with question icon
    int result = MessageBoxW(hParent, message.c_str(), title.c_str(), 
                             MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
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
