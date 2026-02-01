#ifndef BOUNCING_BALL_H
#define BOUNCING_BALL_H

#include <windows.h>

/**
 * BouncingBall - Reusable animated bouncing ball component
 * 
 * A lightweight, self-contained bouncing ball animation that can be embedded
 * into any Windows application. The ball bounces horizontally within its container,
 * and can change color based on application state.
 * 
 * Features:
 * - Smooth horizontal bouncing animation
 * - Customizable colors (e.g., blue for normal, green for downloading)
 * - Automatic timer management
 * - Easy integration with existing Win32 windows
 */

class BouncingBall {
public:
    /**
     * Constructor
     * @param hParent Parent window handle
     * @param x X position within parent
     * @param y Y position within parent
     * @param width Width of animation area
     * @param height Height of animation area (ball will be slightly smaller)
     * @param timerId Unique timer ID for this animation (default: 0xBEEF)
     */
    BouncingBall(HWND hParent, int x, int y, int width, int height, UINT_PTR timerId = 0xBEEF);
    
    /**
     * Destructor - cleans up resources
     */
    ~BouncingBall();
    
    /**
     * Start the animation
     * @param intervalMs Timer interval in milliseconds (default: 16ms for ~60 FPS)
     */
    void Start(int intervalMs = 16);
    
    /**
     * Stop the animation
     */
    void Stop();
    
    /**
     * Set the ball color
     * @param color RGB color value (e.g., RGB(0, 120, 215) for blue)
     */
    void SetColor(COLORREF color);
    
    /**
     * Set color to green (convenience for download/progress state)
     */
    void SetGreenMode();
    
    /**
     * Set color to blue (convenience for normal state)
     */
    void SetBlueMode();
    
    /**
     * Get the window handle of the animation control
     * @return HWND of the animation window
     */
    HWND GetHandle() const { return m_hWnd; }
    
    /**
     * Show the animation window
     */
    void Show();
    
    /**
     * Hide the animation window
     */
    void Hide();

private:
    HWND m_hWnd;           // Animation window handle
    HWND m_hParent;        // Parent window handle
    UINT_PTR m_timerId;    // Timer ID
    int m_animFrame;       // Current animation frame
    COLORREF m_ballColor;  // Current ball color
    bool m_isRunning;      // Animation running state
    int m_dotSize;         // Ball size
    
    // Static window procedure
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    
    // Instance window procedure
    LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    // Paint the animation
    void Paint(HDC hdc, const RECT& rc);
};

#endif // BOUNCING_BALL_H
