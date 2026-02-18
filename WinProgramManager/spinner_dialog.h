#pragma once

#include <windows.h>
#include <string>

// SpinnerDialog - Reusable animated loading spinner dialog
// Displays a blue spinning circle with customizable text message
// Modal dialog that runs in a separate thread with independent message loop
//
// Usage:
//   SpinnerDialog* spinner = new SpinnerDialog(parentHwnd);
//   spinner->Show(L"Processing, please wait...");
//   // ... do work ...
//   spinner->Hide();
//   delete spinner;

class SpinnerDialog {
public:
    // Constructor
    // hParent: Parent window handle (optional, for centering)
    SpinnerDialog(HWND hParent = NULL);
    
    // Destructor - automatically hides and cleans up
    ~SpinnerDialog();
    
    // Show the spinner dialog with custom message
    // text: Message to display below the icon (e.g., "Querying winget, please wait...")
    void Show(const std::wstring& text);
    
    // Hide the spinner dialog
    void Hide();
    
    // Update the displayed text without hiding/showing
    // text: New message to display
    void SetText(const std::wstring& text);
    
    // Check if dialog is currently visible
    bool IsVisible() const;
    
    // Get the dialog window handle (for advanced use)
    HWND GetHandle() const { return m_hDialog; }

private:
    HWND m_hParent;
    HWND m_hDialog;
    HWND m_hSpinnerCtrl;
    HWND m_hTextCtrl;
    int m_spinnerFrame;
    bool m_visible;
    
    // Window procedure
    static LRESULT CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    // Helper to create and center dialog
    void CreateDialogWindow();
    
    // Not copyable
    SpinnerDialog(const SpinnerDialog&) = delete;
    SpinnerDialog& operator=(const SpinnerDialog&) = delete;
};
