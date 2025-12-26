#include "hyperlink.h"
#include <string>
#include <unordered_map>
#include <commctrl.h>
#include <windowsx.h>
#include <vector>

struct HoverInfo { int item; int sub; RECT rect; };
// track hovered item index per list (or -1 for none)
static std::unordered_map<HWND, int> g_hovered_index;

static bool IsPointOverSkip(HWND hList, POINT pt, int &outItem, RECT &outRect) {
    LVHITTESTINFO ht{}; ht.pt = pt;
    int idx = ListView_HitTest(hList, &ht);
    if (idx < 0) return false;
    RECT r; if (!ListView_GetSubItemRect(hList, idx, 3, LVIR_BOUNDS, &r)) return false;
    if (!PtInRectStrict(r, pt)) return false;
    outItem = idx; outRect = r; return true;
}

static LRESULT CALLBACK Hyperlink_ListSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

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

RECT DrawAndTrackHyperlink(HDC hdc, HWND hList, const RECT &rc, const std::wstring &text, HFONT hFont, bool /*hovered*/, int item, int sub) {
    int hoveredItem = -1;
    auto itState = g_hovered_index.find(hList);
    if (itState != g_hovered_index.end()) hoveredItem = itState->second;
    bool hoveredLocal = (hoveredItem == item);
    RECT r = DrawHyperlink(hdc, rc, text, hFont, hoveredLocal);
    return r;
}

void Hyperlink_Clear(HWND hList) {
    g_hovered_index[hList] = -1;
}

void Hyperlink_Attach(HWND hList) {
    if (!hList || !IsWindow(hList)) return;
    SetWindowSubclass(hList, Hyperlink_ListSubclassProc, 0xBEEFBEEF, 0);
}

bool Hyperlink_ProcessMouseMove(HWND hList, POINT pt) {
    int hitIndex = -1; RECT hitRect{};
    bool now = IsPointOverSkip(hList, pt, hitIndex, hitRect);
    int prev = -1;
    auto pit = g_hovered_index.find(hList);
    if (pit != g_hovered_index.end()) prev = pit->second;
    int nowIndex = now ? hitIndex : -1;
    if (nowIndex != prev) {
        if (prev != -1) { RECT prevRect; if (ListView_GetSubItemRect(hList, prev, 3, LVIR_BOUNDS, &prevRect)) InvalidateRect(hList, &prevRect, FALSE); }
        if (now) InvalidateRect(hList, &hitRect, FALSE);
        g_hovered_index[hList] = nowIndex;
        return true;
    }
    return false;
}

static LRESULT CALLBACK Hyperlink_ListSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
        // update hover state; if changed, track and set cursor
        if (Hyperlink_ProcessMouseMove(hwnd, pt)) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
        }
        break;
    }
    case WM_SETCURSOR: {
        // Update hover state and, if cursor over hyperlink, show hand cursor.
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        // ensure hover state is updated so drawing can change color immediately
        Hyperlink_ProcessMouseMove(hwnd, pt);
        int hitItem; RECT hitRect;
        if (IsPointOverSkip(hwnd, pt, hitItem, hitRect)) { SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN: {
        // consume clicks that hit the hyperlink so they don't change selection
        POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
        int hitItem; RECT hitRect;
        if (IsPointOverSkip(hwnd, pt, hitItem, hitRect)) {
            HWND parent = GetParent(hwnd);
            if (parent) PostMessageW(parent, WM_APP + 200, (WPARAM)hitItem, (LPARAM)3);
            return 0; // swallow to prevent selection change
        }
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, Hyperlink_ListSubclassProc, uIdSubclass);
        g_hovered_index.erase(hwnd);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}
