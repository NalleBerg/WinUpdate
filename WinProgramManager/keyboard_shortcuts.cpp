#include "keyboard_shortcuts.h"

HACCEL CreateKeyboardAccelerators() {
    ACCEL accel[3];
    
    // Ctrl+W - Close window
    accel[0].fVirt = FCONTROL | FVIRTKEY;
    accel[0].key = 'W';
    accel[0].cmd = IDM_CLOSE_WINDOW;
    
    // Ctrl+A - Select all
    accel[1].fVirt = FCONTROL | FVIRTKEY;
    accel[1].key = 'A';
    accel[1].cmd = IDM_SELECT_ALL;
    
    // Ctrl+C - Copy
    accel[2].fVirt = FCONTROL | FVIRTKEY;
    accel[2].key = 'C';
    accel[2].cmd = IDM_COPY;
    
    return CreateAcceleratorTable(accel, 3);
}

bool ProcessAccelerator(HACCEL hAccel, MSG* pMsg) {
    if (hAccel && TranslateAccelerator(pMsg->hwnd, hAccel, pMsg)) {
        return true;
    }
    return false;
}
