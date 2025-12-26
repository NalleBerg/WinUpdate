#include "hyperlink.h"
#include <string>

RECT DrawHyperlink(HDC hdc, const RECT &rc, const std::wstring & /*text*/, HFONT hFont, bool hovered) {
    RECT out = rc;
    if (!hdc) return out;
    const std::wstring drawText = L"Skip";
    int saved = SaveDC(hdc);
    HGDIOBJ old = NULL;
    if (hFont) old = SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    COLORREF clr = hovered ? RGB(0, 0, 180) : RGB(0, 0, 238);
    SetTextColor(hdc, clr);

    // measure text
    SIZE sz{};
    GetTextExtentPoint32W(hdc, drawText.c_str(), (int)drawText.size(), &sz);
    int tx = rc.left + ((rc.right - rc.left) - sz.cx) / 2;
    int ty = rc.top + ((rc.bottom - rc.top) - sz.cy) / 2;
    RECT tr = { tx, ty, tx + sz.cx, ty + sz.cy };

    // draw text
    ExtTextOutW(hdc, tr.left, tr.top, ETO_CLIPPED, &rc, drawText.c_str(), (UINT)drawText.size(), NULL);

    // draw underline slightly below text baseline
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    int underlineY = tr.top + tm.tmHeight - 1;
    HPEN pen = CreatePen(PS_SOLID, 1, clr);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, tr.left, underlineY, NULL);
    LineTo(hdc, tr.right, underlineY);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    if (old) SelectObject(hdc, old);
    RestoreDC(hdc, saved);
    return tr;
}

bool PtInRectStrict(const RECT &r, POINT pt) {
    return (pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom);
}
