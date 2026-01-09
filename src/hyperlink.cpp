#include "hyperlink.h"
#include "skip_confirm_dialog.h"
#include "exclude_confirm_dialog.h"
#include <string>
#include "logging.h"
#include "parsing.h"
#include "skip_update.h"
#include "exclude.h"
#include <unordered_map>
#include <commctrl.h>
#include <windowsx.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>
#include <functional>
#include <sstream>
#include <mutex>

// External declarations from main.cpp
extern std::vector<std::pair<std::string,std::string>> g_packages;
extern std::mutex g_packages_mutex;

// Button IDs from main.cpp
#define IDC_BTN_REFRESH 1003

struct HoverInfo { int item; int sub; RECT rect; };
// track hovered item index per list (or -1 for none)
static std::unordered_map<HWND, int> g_hovered_index;
// tooltip window per list
static std::unordered_map<HWND, HWND> g_tooltips;
// persistent tooltip text buffer per list (to keep pointer valid for TOOLINFOW)
static std::unordered_map<HWND, std::wstring> g_tooltip_texts;

static bool IsPointOverSkip(HWND hList, POINT pt, int &outItem, RECT &outRect) {
    LVHITTESTINFO ht{}; ht.pt = pt;
    int idx = ListView_HitTest(hList, &ht);
    if (idx < 0) return false;
    RECT r; if (!ListView_GetSubItemRect(hList, idx, 3, LVIR_BOUNDS, &r)) return false;
    if (!PtInRectStrict(r, pt)) return false;
    outItem = idx; outRect = r; return true;
}

static bool IsPointOverExclude(HWND hList, POINT pt, int &outItem, RECT &outRect) {
    LVHITTESTINFO ht{}; ht.pt = pt;
    int idx = ListView_HitTest(hList, &ht);
    if (idx < 0) return false;
    RECT r; if (!ListView_GetSubItemRect(hList, idx, 4, LVIR_BOUNDS, &r)) return false;
    if (!PtInRectStrict(r, pt)) return false;
    outItem = idx; outRect = r; return true;
}

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

static std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return std::string(w.begin(), w.end());
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], needed, NULL, NULL);
    return out;
}

static std::string ReadFileToString(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return std::string();
    std::ostringstream ss; ss << ifs.rdbuf();
    return ss.str();
}

static std::string GetSettingsIniPath() {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    std::string path;
    if (len > 0 && len < MAX_PATH) {
        path = std::string(buf) + "\\WinUpdate";
    } else {
        path = "."; // fallback to current dir
    }
    // ensure directory exists
    std::wstring wpath;
    int nw = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, NULL, 0);
    if (nw > 0) {
        std::vector<wchar_t> wb(nw);
        MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, wb.data(), nw);
        wpath = wb.data();
        CreateDirectoryW(wpath.c_str(), NULL);
    }
    std::string ini = path + "\\wup_settings.ini";
    return ini;
}

static std::string LoadLocaleSetting() {
    std::string ini = GetSettingsIniPath();
    std::ifstream ifs(ini, std::ios::binary);
    if (!ifs) return std::string("en_GB");
    
    auto trim = [](std::string &s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b-a+1); };
    std::string line;
    bool inLang = false;
    while (std::getline(ifs, line)) {
        trim(line);
        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') {
            inLang = (line == "[language]");
            continue;
        }
        if (inLang) return line; // Return full locale code (en_GB, nb_NO, sv_SE)
    }
    return std::string("en_GB");
}

static std::string LoadI18nValue(const std::string &locale, const std::string &key) {
    std::string fn = std::string("locale/") + locale + ".txt";
    std::string txt = ReadFileToString(fn);
    if (txt.empty()) return std::string();
    std::istringstream iss(txt);
    std::string ln;
    while (std::getline(iss, ln)) {
        if (ln.empty()) continue;
        if (ln[0] == '#' || ln[0] == ';') continue;
        size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        std::string k = ln.substr(0, eq);
        std::string v = ln.substr(eq+1);
        if (k == key) return v;
    }
    return std::string();
}

static bool g_tipClassRegistered = false;
static const wchar_t *kTipClassName = L"HyperlinkCustomTip";
static const int kTipPadX = 30;
static const int kTipPadY = 12;

    static void EnsureTipClassRegistered() {
    if (g_tipClassRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_NOCLOSE;
        wc.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)->LRESULT {
        switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            // background
            HBRUSH hbr = CreateSolidBrush(GetSysColor(COLOR_INFOBK));
            FillRect(hdc, &rc, hbr);
            DeleteObject(hbr);
            // border
            FrameRect(hdc, &rc, GetSysColorBrush(COLOR_WINDOWFRAME));
            // draw text stored in window property with inset padding so text isn't clipped
            // retrieve window text dynamically to avoid truncation for long strings
            int len = GetWindowTextLengthW(hwnd);
            std::vector<wchar_t> buf(len + 1);
            if (len > 0) GetWindowTextW(hwnd, buf.data(), len + 1);
            else buf[0] = 0;
            SetTextColor(hdc, GetSysColor(COLOR_INFOTEXT));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
            HGDIOBJ oldf = NULL;
            if (hf) oldf = SelectObject(hdc, hf);
            RECT inner = rc;
            // inset by symmetric padding on both sides so text isn't clipped
            inner.left += kTipPadX;
            inner.right -= kTipPadX;
            inner.top += kTipPadY;
            inner.bottom -= kTipPadY;
            DrawTextW(hdc, buf.data(), -1, &inner, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
            if (oldf) SelectObject(hdc, oldf);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCDESTROY:
            break;
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    };
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
    wc.lpszClassName = kTipClassName;
    RegisterClassExW(&wc);
    g_tipClassRegistered = true;
}

static HWND EnsureTooltipForList(HWND hList) {
    auto it = g_tooltips.find(hList);
    if (it != g_tooltips.end() && IsWindow(it->second)) return it->second;
    EnsureTipClassRegistered();
    HWND owner = GetAncestor(hList, GA_ROOT);
    if (!owner) owner = GetParent(hList);
    // create a lightweight popup window (no activate) and keep it hidden until needed
    // Do NOT use WS_EX_TOPMOST — topmost tooltips will stay above other apps
    // and can overlay during Alt+Tab. Keep the tooltip non-topmost and
    // use SWP_NOACTIVATE when showing so it does not steal focus.
    HWND tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kTipClassName, L"", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!tip) return NULL;
    // ensure it does not take focus; do NOT force topmost so it won't overlay other apps
    SetWindowPos(tip, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    g_tooltips[hList] = tip;
    g_tooltip_texts[hList] = L"";
    AppendLog("[hyperlink] Created custom tooltip popup\n");
    // Use the list's font for the tip so measurement/painting match
    HFONT lf = (HFONT)SendMessageW(hList, WM_GETFONT, 0, 0);
    if (lf) SendMessageW(tip, WM_SETFONT, (WPARAM)lf, TRUE);
    return tip;
}

static std::wstring FormatTooltipTemplate(const std::wstring &tmpl, const std::wstring &appname) {
    // replace %s or <appname> tokens with appname
    std::wstring out = tmpl;
    size_t p = out.find(L"%s");
    if (p != std::wstring::npos) { out.replace(p, 2, appname); return out; }
    p = out.find(L"<appname>");
    if (p != std::wstring::npos) { out.replace(p, wcslen(L"<appname>"), appname); return out; }
    return out + L" " + appname;
}

static LRESULT CALLBACK Hyperlink_ListSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

RECT DrawHyperlink(HDC hdc, const RECT &rc, const std::wstring &text, HFONT hFont, bool hovered) {
    RECT out = rc;
    if (!hdc) return out;
    const std::wstring drawText = text.empty() ? L"Skip" : text;
    int saved = SaveDC(hdc);
    HGDIOBJ old = NULL;
    if (hFont) old = SelectObject(hdc, hFont);
    
    // Fill background to cover any text the ListView drew
    HBRUSH bgBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    FillRect(hdc, &rc, bgBrush);
    
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
    bool nowSkip = IsPointOverSkip(hList, pt, hitIndex, hitRect);
    bool nowExclude = false;
    if (!nowSkip) nowExclude = IsPointOverExclude(hList, pt, hitIndex, hitRect);
    bool now = nowSkip || nowExclude;
    int prev = -1;
    auto pit = g_hovered_index.find(hList);
    if (pit != g_hovered_index.end()) prev = pit->second;
    int nowIndex = now ? hitIndex : -1;
    if (nowIndex != prev) {
        if (prev != -1) {
            RECT prevRect;
            if (ListView_GetSubItemRect(hList, prev, 3, LVIR_BOUNDS, &prevRect)) InvalidateRect(hList, &prevRect, FALSE);
            if (ListView_GetSubItemRect(hList, prev, 4, LVIR_BOUNDS, &prevRect)) InvalidateRect(hList, &prevRect, FALSE);
        }
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
        // ensure we get a WM_MOUSELEAVE when the pointer leaves the control
        {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }

        // show tooltip when hovering over Skip or Exclude
        int hitIndex = -1; RECT hitRect{};
        bool overSkip = IsPointOverSkip(hwnd, pt, hitIndex, hitRect);
        bool overExclude = false;
        if (!overSkip) overExclude = IsPointOverExclude(hwnd, pt, hitIndex, hitRect);
        if (overSkip || overExclude) {
            HWND tt = EnsureTooltipForList(hwnd);
            if (tt) {
                // build tooltip text using i18n
                std::string locale = LoadLocaleSetting(); if (locale.empty()) locale = "en_GB";
                std::string tmpl;
                if (overSkip) {
                    tmpl = LoadI18nValue(locale, "skip_tooltip");
                    if (tmpl.empty()) tmpl = LoadI18nValue(locale, "skip_confirm_question");
                } else {
                    tmpl = LoadI18nValue(locale, "exclude_tooltip");
                    if (tmpl.empty()) tmpl = LoadI18nValue(locale, "confirm_exclude");
                }
                std::wstring wtmpl = Utf8ToWide(tmpl);
                // get app name from list item text (subitem 0)
                wchar_t buf[512]; buf[0]=0;
                LVITEMW lvi{}; lvi.iSubItem = 0; lvi.cchTextMax = _countof(buf); lvi.pszText = buf; lvi.iItem = hitIndex; SendMessageW(hwnd, LVM_GETITEMTEXTW, (WPARAM)hitIndex, (LPARAM)&lvi);
                std::wstring appname = buf[0] ? std::wstring(buf) : std::wstring();
                std::wstring final = FormatTooltipTemplate(wtmpl, appname);
                g_tooltip_texts[hwnd] = final;
                AppendLog("[hyperlink] Updating custom tooltip text and showing\n");
                HWND tip = EnsureTooltipForList(hwnd);
                if (tip && IsWindow(tip)) {
                    // set text and size
                    SetWindowTextW(tip, g_tooltip_texts[hwnd].c_str());
                    // measure text precisely and apply symmetric padding on both sides
                    const int padX = kTipPadX, padY = kTipPadY;
                    HDC hdc = GetDC(tip);
                    HFONT hf = (HFONT)SendMessageW(tip, WM_GETFONT, 0, 0);
                    HGDIOBJ oldf = NULL;
                    if (hf) oldf = SelectObject(hdc, hf);
                    RECT rcCalc = {0,0,0,0};
                    // DT_CALCRECT with DT_SINGLELINE measures the required width/height
                    DrawTextW(hdc, g_tooltip_texts[hwnd].c_str(), -1, &rcCalc, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
                    int naturalW = rcCalc.right - rcCalc.left;
                    int naturalH = rcCalc.bottom - rcCalc.top;
                    int w = naturalW + (padX * 2);
                    int h = naturalH + (padY * 2);
                    if (oldf) SelectObject(hdc, oldf);
                    ReleaseDC(tip, hdc);
                    POINT tl = { hitRect.left, hitRect.top }; POINT br = { hitRect.right, hitRect.bottom };
                    ClientToScreen(hwnd, &tl); ClientToScreen(hwnd, &br);
                    int tipX = (tl.x + br.x) / 2 - w/2;
                    int tipY = tl.y - h - 8; // place above
                    // constrain to work area (prevent off-screen)
                    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
                    if (tipX < wa.left) tipX = wa.left + 4;
                    if (tipX + w > wa.right) tipX = wa.right - w - 4;
                    if (tipY < wa.top) tipY = br.y + 8; // if no space above, show below
                    // Do not force topmost; position above other windows but don't steal z-order
                    SetWindowPos(tip, HWND_TOP, tipX, tipY, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    InvalidateRect(tip, NULL, TRUE);
                    AppendLog("[hyperlink] custom tooltip shown\n");
                }
            }
            } else {
            // hide custom tooltip if present
            auto it = g_tooltips.find(hwnd);
            if (it != g_tooltips.end() && IsWindow(it->second)) {
                AppendLog("[hyperlink] Hiding custom tooltip\n");
                ShowWindow(it->second, SW_HIDE);
            }
        }
        break;
    }
    case WM_MOUSELEAVE: {
        auto it = g_tooltips.find(hwnd);
        if (it != g_tooltips.end() && IsWindow(it->second)) {
            AppendLog("[hyperlink] WM_MOUSELEAVE - hiding custom tooltip\n");
            ShowWindow(it->second, SW_HIDE);
        }
        break;
    }
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED: {
        auto it = g_tooltips.find(hwnd);
        if (it != g_tooltips.end() && IsWindow(it->second)) {
            AppendLog("[hyperlink] focus/capture lost - hiding custom tooltip\n");
            ShowWindow(it->second, SW_HIDE);
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
        if (IsPointOverExclude(hwnd, pt, hitItem, hitRect)) { SetCursor(LoadCursor(NULL, IDC_HAND)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN: {
        // consume clicks that hit the hyperlink so they don't change selection
        POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
        int hitItem; RECT hitRect;
        // Handle Exclude column clicks
        if (IsPointOverExclude(hwnd, pt, hitItem, hitRect)) {
            HWND parent = GetParent(hwnd);
            LVITEMW lvi{}; lvi.iItem = hitItem; lvi.mask = LVIF_PARAM;
            SendMessageW(hwnd, LVM_GETITEMW, 0, (LPARAM)&lvi);
            int pkgIdx = (int)lvi.lParam;
            if (pkgIdx >= 0 && pkgIdx < (int)g_packages.size()) {
                std::string id = g_packages[pkgIdx].first;
                std::string name = g_packages[pkgIdx].second;
                std::wstring wname = Utf8ToWide(name);
                
                if (IsExcluded(id)) {
                    // Already excluded - simple confirmation for unexclude
                    std::string locale = LoadLocaleSetting();
                    if (locale.empty()) locale = "en_GB";
                    std::string tmpl = LoadI18nValue(locale, "confirm_unexclude");
                    std::wstring msg = Utf8ToWide(tmpl);
                    std::wstring title = Utf8ToWide(LoadI18nValue(locale, "app_title"));
                    if (MessageBoxW(parent, msg.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        UnexcludeApp(id);
                        if (parent) PostMessageW(parent, WM_COMMAND, MAKEWPARAM(IDC_BTN_REFRESH, BN_CLICKED), 0);
                    }
                } else {
                    // Not excluded - show custom dialog
                    if (ShowExcludeConfirm(parent, wname)) {
                        ExcludeApp(id, "manual");
                        if (parent) PostMessageW(parent, WM_COMMAND, MAKEWPARAM(IDC_BTN_REFRESH, BN_CLICKED), 0);
                    }
                }
            }
            return 0; // consume the click
        }
        if (IsPointOverSkip(hwnd, pt, hitItem, hitRect)) {
            HWND parent = GetParent(hwnd);
            // retrieve app name (subitem 0) and available version (subitem 2) for the confirm dialog
            wchar_t appbuf[512] = {0};
            LVITEMW lviApp{}; lviApp.iSubItem = 0; lviApp.cchTextMax = _countof(appbuf); lviApp.pszText = appbuf; lviApp.iItem = hitItem;
            SendMessageW(hwnd, LVM_GETITEMTEXTW, (WPARAM)hitItem, (LPARAM)&lviApp);
            std::wstring appname = appbuf;
            wchar_t availbuf[128] = {0};
            LVITEMW lviAvail{}; lviAvail.iSubItem = 2; lviAvail.cchTextMax = _countof(availbuf); lviAvail.pszText = availbuf; lviAvail.iItem = hitItem;
            SendMessageW(hwnd, LVM_GETITEMTEXTW, (WPARAM)hitItem, (LPARAM)&lviAvail);
            std::wstring avail = availbuf;
            // show localized confirmation immediately; if confirmed, notify parent to perform skip
            bool ok = false;
            try {
                AppendLog(std::string("[hyperlink] invoking ShowSkipConfirm for app=") + WideToUtf8(appname) + " avail=" + WideToUtf8(avail) + "\n");
                ok = ShowSkipConfirm(parent ? parent : hwnd, appname, avail);
                AppendLog(std::string("[hyperlink] ShowSkipConfirm returned=") + (ok?"1":"0") + "\n");
            } catch (...) {
                AppendLog("[hyperlink] ShowSkipConfirm threw exception\n");
                ok = false;
            }
            if (ok) {
                // Post skip request to the top-level window (ancestor) to ensure
                // the main window procedure receives the message even if the
                // direct parent is a nested control.
                // Prefer posting directly to the main application window by class name
                // to ensure the correct window procedure receives the skip request.
                HWND mainWnd = FindWindowW(L"WinUpdateClass", NULL);
                if (!mainWnd) {
                    // fallback: use ancestor of parent
                    if (parent) mainWnd = GetAncestor(parent, GA_ROOT);
                    if (!mainWnd) mainWnd = GetAncestor(hwnd, GA_ROOT);
                }
                if (mainWnd) {
                    // Retrieve the item's lParam (which holds the package index into g_packages)
                    LPARAM pkgIdx = (LPARAM)hitItem; // fallback to visual index
                    try {
                        LVITEMW lvi{}; lvi.iItem = hitItem; lvi.mask = LVIF_PARAM; SendMessageW(hwnd, LVM_GETITEMW, (WPARAM)hitItem, (LPARAM)&lvi); pkgIdx = lvi.lParam;
                    } catch(...) {}
                    // Diagnostic pop-up to ensure the click produced expected values
                    try {
                        std::wstring dbg = L"Skip click captured:\napp=" + std::wstring((wchar_t*)0) + L"\n";
                        // Build a small diagnostic showing indices and available text
                        wchar_t bufApp[256] = {0}; LVITEMW lviApp{}; lviApp.iItem = hitItem; lviApp.iSubItem = 0; lviApp.cchTextMax = _countof(bufApp); lviApp.pszText = bufApp; lviApp.mask = LVIF_TEXT; SendMessageW(hwnd, LVM_GETITEMW, (WPARAM)hitItem, (LPARAM)&lviApp);
                        wchar_t bufAvail[128] = {0}; LVITEMW lviAvail{}; lviAvail.iItem = hitItem; lviAvail.iSubItem = 2; lviAvail.cchTextMax = _countof(bufAvail); lviAvail.pszText = bufAvail; lviAvail.mask = LVIF_TEXT; SendMessageW(hwnd, LVM_GETITEMW, (WPARAM)hitItem, (LPARAM)&lviAvail);
                        std::wstring sdbg = L"app=" + std::wstring(bufApp) + L"\navail=" + std::wstring(bufAvail) + L"\nhitIndex=" + std::to_wstring(hitItem) + L"\nlParam(pkgIdx)=" + std::to_wstring((long long)pkgIdx);
                        MessageBoxW(parent, sdbg.c_str(), L"DEBUG: skip click", MB_OK | MB_ICONINFORMATION);
                    } catch(...) {}
                    AppendLog(std::string("[hyperlink] sending WM_COPYDATA to mainWnd=") + std::to_string((uintptr_t)mainWnd) + " pkgIdx=" + std::to_string((long long)pkgIdx) + "\n");
                    // Build payload as used by skip_confirm_dialog: "WUP_SKIP\n<appname>\n<available>\n"
                    try {
                        wchar_t bufApp[256] = {0}; LVITEMW lviApp{}; lviApp.iItem = hitItem; lviApp.iSubItem = 0; lviApp.cchTextMax = _countof(bufApp); lviApp.pszText = bufApp; lviApp.mask = LVIF_TEXT; SendMessageW(hwnd, LVM_GETITEMW, (WPARAM)hitItem, (LPARAM)&lviApp);
                        wchar_t bufAvail[128] = {0}; LVITEMW lviAvail{}; lviAvail.iItem = hitItem; lviAvail.iSubItem = 2; lviAvail.cchTextMax = _countof(bufAvail); lviAvail.pszText = bufAvail; lviAvail.mask = LVIF_TEXT; SendMessageW(hwnd, LVM_GETITEMW, (WPARAM)hitItem, (LPARAM)&lviAvail);
                        // Convert to UTF-8
                        std::string appn_utf8; int needed = WideCharToMultiByte(CP_UTF8, 0, bufApp, -1, NULL, 0, NULL, NULL);
                        if (needed>0) { std::vector<char> b(needed); WideCharToMultiByte(CP_UTF8,0,bufApp,-1,b.data(),needed,NULL,NULL); appn_utf8 = b.data(); }
                        std::string ver_utf8; needed = WideCharToMultiByte(CP_UTF8, 0, bufAvail, -1, NULL, 0, NULL, NULL);
                        if (needed>0) { std::vector<char> b2(needed); WideCharToMultiByte(CP_UTF8,0,bufAvail,-1,b2.data(),needed,NULL,NULL); ver_utf8 = b2.data(); }
                        std::string payload = std::string("WUP_SKIP\n") + appn_utf8 + "\n" + ver_utf8 + "\n";
                        // Ensure persistence locally first (use lParam pkg index if present)
                        try {
                            auto resolveId = [&](const std::string &name, int index)->std::string {
                                std::string out;
                                try {
                                    std::lock_guard<std::mutex> lk(g_packages_mutex);
                                    if (index >= 0 && index < (int)g_packages.size()) out = g_packages[index].first;
                                    if (!out.empty()) return out;
                                    // exact match
                                    for (int i = 0; i < (int)g_packages.size(); ++i) if (g_packages[i].second == name) return g_packages[i].first;
                                    // case-insensitive and substring matches
                                    std::string name_l = name; for (auto &c : name_l) c = (char)tolower((unsigned char)c);
                                    for (int i = 0; i < (int)g_packages.size(); ++i) {
                                        std::string nm = g_packages[i].second; std::string nm_l = nm; for (auto &c : nm_l) c = (char)tolower((unsigned char)c);
                                        if (nm_l == name_l || nm_l.find(name_l) != std::string::npos || name_l.find(nm_l) != std::string::npos) return g_packages[i].first;
                                    }
                                } catch(...) {}
                                return std::string();
                            };
                            std::string id;
                            int pi = (int)pkgIdx;
                            id = resolveId(appn_utf8, pi);
                            // If still not found, try repopulating g_packages from recent raw winget output
                            if (id.empty()) {
                                try {
                                    AppendLog("[hyperlink] pre-send: attempting to repopulate g_packages from raw winget\n");
                                    std::string raw = ReadMostRecentRawWinget();
                                    if (!raw.empty()) {
                                        ParseWingetTextForPackages(raw);
                                        AppendLog(std::string("[hyperlink] pre-send: ParseWingetTextForPackages populated g_packages size=") + std::to_string((int)g_packages.size()) + "\n");
                                    }
                                } catch(...) { AppendLog("[hyperlink] pre-send: repopulate attempt threw\n"); }
                                id = resolveId(appn_utf8, pi);
                            }
                            if (!id.empty()) {
                                // Get the full display name from g_packages using the resolved ID
                                std::string fullDisplayName = appn_utf8;
                                try {
                                    std::lock_guard<std::mutex> lk(g_packages_mutex);
                                    for (auto &pkg : g_packages) {
                                        if (pkg.first == id) {
                                            fullDisplayName = pkg.second;
                                            break;
                                        }
                                    }
                                } catch(...) {}
                                AppendLog(std::string("[hyperlink] pre-send local AddSkippedEntry id=") + id + " ver=" + ver_utf8 + " name=" + fullDisplayName + "\n");
                                bool ok = AddSkippedEntry(id, ver_utf8, fullDisplayName);
                                AppendLog(std::string("[hyperlink] pre-send AddSkippedEntry result=") + (ok?"OK":"FAIL") + "\n");
                                // Don't send WM_COPYDATA here - just let the post-dialog code handle refresh
                            } else {
                                AppendLog("[hyperlink] pre-send: could not resolve id for local persistence, appending raw\n");
                                // Check if refresh is in progress before allowing skip
                                if (g_refresh_in_progress.load()) {
                                    AppendLog("[hyperlink] Skip blocked - refresh in progress\n");
                                    MessageBoxA(NULL, "Please wait for the list to finish loading.", "WinUpdate", MB_OK | MB_ICONINFORMATION);
                                    return 0;  // Return proper LRESULT
                                }
                                try {
                                    bool ok2 = AppendSkippedRaw(appn_utf8, ver_utf8);
                                    AppendLog(std::string("[hyperlink] pre-send AppendSkippedRaw result=") + (ok2?"OK":"FAIL") + "\n");
                                } catch(...) { AppendLog("[hyperlink] pre-send AppendSkippedRaw threw\n"); }
                            }
                        } catch(...) { AppendLog("[hyperlink] pre-send local AddSkippedEntry threw\n"); }

                        // Skip sending WM_COPYDATA since we already persisted locally
                        // Just trigger an asynchronous refresh
                        PostMessageW(mainWnd, WM_APP + 1, (WPARAM)1, (LPARAM)0);  // WM_REFRESH_ASYNC
                        AppendLog("[hyperlink] Posted WM_REFRESH_ASYNC after skip\n");
                    } catch(...) { AppendLog("[hyperlink] Skip handling threw exception\n"); }
                } else {
                    AppendLog("[hyperlink] failed to find main window to post skip message\n");
                }
            }
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
