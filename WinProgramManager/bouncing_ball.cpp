#include "bouncing_ball.h"
#include <commctrl.h>

BouncingBall::BouncingBall(HWND hParent, int x, int y, int width, int height, UINT_PTR timerId)
    : m_hParent(hParent)
    , m_timerId(timerId)
    , m_animFrame(0)
    , m_ballColor(RGB(0, 120, 215))  // Default blue
    , m_isRunning(false)
    , m_dotSize(height - 22)  // Small and compact (18px with 40px height)
{
    // Create animation window
    m_hWnd = CreateWindowExW(
        0,
        L"Static",
        L"",
        WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        hParent,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    if (m_hWnd) {
        // Subclass the window to handle custom painting
        SetWindowSubclass(m_hWnd, StaticWndProc, 0, (DWORD_PTR)this);
    }
}

BouncingBall::~BouncingBall() {
    Stop();
    if (m_hWnd) {
        RemoveWindowSubclass(m_hWnd, StaticWndProc, 0);
        DestroyWindow(m_hWnd);
    }
}

void BouncingBall::Start(int intervalMs) {
    if (!m_isRunning && m_hWnd) {
        m_isRunning = true;
        m_animFrame = 0;
        SetTimer(m_hWnd, m_timerId, intervalMs, NULL);
    }
}

void BouncingBall::Stop() {
    if (m_isRunning && m_hWnd) {
        m_isRunning = false;
        KillTimer(m_hWnd, m_timerId);
    }
}

void BouncingBall::SetColor(COLORREF color) {
    m_ballColor = color;
    if (m_hWnd) {
        InvalidateRect(m_hWnd, NULL, TRUE);
    }
}

void BouncingBall::SetGreenMode() {
    SetColor(RGB(84, 176, 84));
}

void BouncingBall::SetBlueMode() {
    SetColor(RGB(0, 120, 215));
}

void BouncingBall::Show() {
    if (m_hWnd) {
        ShowWindow(m_hWnd, SW_SHOW);
    }
}

void BouncingBall::Hide() {
    if (m_hWnd) {
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

LRESULT CALLBACK BouncingBall::StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    (void)uIdSubclass;  // Unused parameter
    BouncingBall* pThis = reinterpret_cast<BouncingBall*>(dwRefData);
    if (pThis) {
        return pThis->WndProc(hwnd, uMsg, wParam, lParam);
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT BouncingBall::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TIMER: {
            if (wParam == m_timerId) {
                m_animFrame++;
                InvalidateRect(hwnd, NULL, TRUE);
                return 0;
            }
            break;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            Paint(hdc, rc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_ERASEBKGND: {
            // Let WM_PAINT handle all drawing
            return 1;
        }
    }
    
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void BouncingBall::Paint(HDC hdc, const RECT& rc) {
    // Clear the entire area with parent background color
    HBRUSH hBgBrush = (HBRUSH)GetClassLongPtr(m_hParent, GCLP_HBRBACKGROUND);
    if (!hBgBrush) hBgBrush = GetSysColorBrush(COLOR_BTNFACE);
    FillRect(hdc, &rc, hBgBrush);
    
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    
    // Movement range (dot travels from left edge to right edge)
    int travelDistance = width - m_dotSize;
    
    // Bounce back and forth: double the cycle length
    int cycleLength = travelDistance * 2;
    int posInCycle = (m_animFrame * 1) % cycleLength;
    
    int position;
    if (posInCycle <= travelDistance) {
        // Moving forward
        position = posInCycle;
    } else {
        // Moving backward
        position = travelDistance - (posInCycle - travelDistance);
    }
    
    // Draw the round dot
    HBRUSH hBrush = CreateSolidBrush(m_ballColor);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    HPEN hPen = CreatePen(PS_SOLID, 1, m_ballColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    
    int centerX = position + m_dotSize / 2;
    int centerY = height / 2;
    int radius = m_dotSize / 2;
    
    Ellipse(hdc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
    
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}
