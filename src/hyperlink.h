#pragma once
#include <windows.h>
#include <string>

// Draws a hyperlink-style text inside rc using hFont (if non-null).
// Returns the rectangle actually used to draw the text (in the same coordinates as rc).
RECT DrawHyperlink(HDC hdc, const RECT &rc, const std::wstring &text, HFONT hFont, bool hovered);

// Returns true if point (client coords of the control) lies inside the given text rect.
bool PtInRectStrict(const RECT &r, POINT pt);
