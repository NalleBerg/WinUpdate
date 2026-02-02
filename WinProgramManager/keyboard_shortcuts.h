#ifndef KEYBOARD_SHORTCUTS_H
#define KEYBOARD_SHORTCUTS_H

#include <windows.h>

// Command IDs for keyboard shortcuts
#define IDM_CLOSE_WINDOW 9001
#define IDM_SELECT_ALL 9002
#define IDM_COPY 9003

// Create accelerator table for common keyboard shortcuts
HACCEL CreateKeyboardAccelerators();

// Process accelerator messages in message loop
// Returns true if message was processed
bool ProcessAccelerator(HACCEL hAccel, MSG* pMsg);

#endif // KEYBOARD_SHORTCUTS_H
