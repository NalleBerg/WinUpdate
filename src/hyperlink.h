#pragma once
#include <windows.h>
#include <string>

// Draws a hyperlink-style text inside rc using hFont (if non-null).
// Returns the rectangle actually used to draw the text (in the same coordinates as rc).
RECT DrawHyperlink(HDC hdc, const RECT &rc, const std::wstring &text, HFONT hFont, bool hovered);

// Returns true if point (client coords of the control) lies inside the given text rect.
bool PtInRectStrict(const RECT &r, POINT pt);

// Draw and track hyperlink rect for the given list control item/subitem.
RECT DrawAndTrackHyperlink(HDC hdc, HWND hList, const RECT &rc, const std::wstring &text, HFONT hFont, bool hovered, int item, int sub);

// Subclass/attach the hyperlink behavior to a ListView control.
void Hyperlink_Attach(HWND hList);

// Clear tracked hyperlink rects for a list prior to painting.
void Hyperlink_Clear(HWND hList);

// Process a mouse move externally (optional). Returns true if hover state changed and control invalidated.
bool Hyperlink_ProcessMouseMove(HWND hList, POINT pt);

