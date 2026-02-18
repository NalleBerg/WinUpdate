#include "spinner_dialog.h"
#include "resource.h"
#include <map>

// Static map to associate HWND with SpinnerDialog instance
static std::map<HWND, SpinnerDialog*> g_spinnerInstances;

SpinnerDialog::SpinnerDialog(HWND hParent)
    : m_hParent(hParent)
    , m_hDialog(NULL)
    , m_hSpinnerCtrl(NULL)
    , m_hTextCtrl(NULL)
    , m_spinnerFrame(0)
    , m_visible(false)
{
}

SpinnerDialog::~SpinnerDialog() {
    Hide();
}

void SpinnerDialog::CreateDialogWindow() {
    if (m_hDialog && IsWindow(m_hDialog)) {
        return; // Already created
    }
    
    HINSTANCE hInstance = GetModuleHandle(NULL);
    
    // Register window class
    static bool classRegistered = false;
    if (!classRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"SpinnerDialogClass";
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        classRegistered = true;
    }
    
    // Create dialog window
    m_hDialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"SpinnerDialogClass",
        L"Please Wait",
        WS_POPUP | WS_CAPTION,
        0, 0, 400, 250,
        m_hParent, NULL, hInstance, NULL
    );
    
    if (!m_hDialog) return;
    
    // Store instance pointer for window procedure
    g_spinnerInstances[m_hDialog] = this;
    
    // Center dialog
    RECT rc;
    GetWindowRect(m_hDialog, &rc);
    int dialogWidth = rc.right - rc.left;
    int dialogHeight = rc.bottom - rc.top;
    
    int x, y;
    if (m_hParent && IsWindow(m_hParent)) {
        // Center on parent
        RECT parentRc;
        GetWindowRect(m_hParent, &parentRc);
        x = parentRc.left + (parentRc.right - parentRc.left - dialogWidth) / 2;
        y = parentRc.top + (parentRc.bottom - parentRc.top - dialogHeight) / 2;
    } else {
        // Center on screen
        x = (GetSystemMetrics(SM_CXSCREEN) - dialogWidth) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - dialogHeight) / 2;
    }
    SetWindowPos(m_hDialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    
    // Add icon
    HICON hIcon = LoadIcon(NULL, IDI_INFORMATION);
    if (hIcon) {
        HWND hIconCtrl = CreateWindowExW(0, L"STATIC", NULL, 
            WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            150, 20, 100, 100, m_hDialog, NULL, hInstance, NULL);
        SendMessageW(hIconCtrl, STM_SETICON, (WPARAM)hIcon, 0);
    }
    
    // Add text label
    m_hTextCtrl = CreateWindowExW(0, L"STATIC", L"Please wait...", 
        WS_CHILD | WS_VISIBLE | SS_CENTER, 
        20, 130, 360, 30, m_hDialog, NULL, hInstance, NULL);
    
    HFONT hTextFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_hTextCtrl, WM_SETFONT, (WPARAM)hTextFont, TRUE);
    
    // Add spinner
    m_hSpinnerCtrl = CreateWindowExW(0, L"STATIC", L"◐", 
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        150, 170, 100, 60, m_hDialog, NULL, hInstance, NULL);
    
    HFONT hSpinnerFont = CreateFontW(50, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(m_hSpinnerCtrl, WM_SETFONT, (WPARAM)hSpinnerFont, TRUE);
    
    // Start timer
    SetTimer(m_hDialog, 1, 60, NULL);
}

void SpinnerDialog::Show(const std::wstring& text) {
    CreateDialogWindow();
    if (!m_hDialog) return;
    
    SetText(text);
    ShowWindow(m_hDialog, SW_SHOW);
    UpdateWindow(m_hDialog);
    m_visible = true;
    
    // Process a few messages to let dialog initialize
    MSG msg;
    for (int i = 0; i < 5; i++) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16);
    }
}

void SpinnerDialog::Hide() {
    if (m_hDialog && IsWindow(m_hDialog)) {
        KillTimer(m_hDialog, 1);
        g_spinnerInstances.erase(m_hDialog);
        DestroyWindow(m_hDialog);
        m_hDialog = NULL;
        m_hSpinnerCtrl = NULL;
        m_hTextCtrl = NULL;
    }
    m_visible = false;
}

void SpinnerDialog::SetText(const std::wstring& text) {
    if (m_hTextCtrl && IsWindow(m_hTextCtrl)) {
        SetWindowTextW(m_hTextCtrl, text.c_str());
        UpdateWindow(m_hTextCtrl);
    }
}

bool SpinnerDialog::IsVisible() const {
    return m_visible && m_hDialog && IsWindow(m_hDialog);
}

LRESULT CALLBACK SpinnerDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    
    // Get instance from map
    auto it = g_spinnerInstances.find(hwnd);
    SpinnerDialog* pThis = (it != g_spinnerInstances.end()) ? it->second : nullptr;
    
    switch (uMsg) {
        case WM_TIMER:
            if (wParam == 1 && pThis) {
                const wchar_t* frames[] = { L"◐", L"◓", L"◑", L"◒" };
                pThis->m_spinnerFrame = (pThis->m_spinnerFrame + 1) % 4;
                if (pThis->m_hSpinnerCtrl && IsWindow(pThis->m_hSpinnerCtrl)) {
                    SetWindowTextW(pThis->m_hSpinnerCtrl, frames[pThis->m_spinnerFrame]);
                }
            }
            return 0;
            
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            
            SetBkMode(hdcStatic, OPAQUE);
            SetBkColor(hdcStatic, RGB(255, 255, 255));
            
            // Make spinner blue
            if (pThis && hStatic == pThis->m_hSpinnerCtrl) {
                SetTextColor(hdcStatic, RGB(0, 120, 215));
            } else {
                SetTextColor(hdcStatic, RGB(0, 0, 0));
            }
            
            return (LRESULT)hWhiteBrush;
        }
        
        case WM_CLOSE:
            // Don't allow user to close
            return 0;
    }
    
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
