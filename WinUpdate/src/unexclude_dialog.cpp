#include "unexclude_dialog.h"
#include "exclude.h"
#include "Config.h"
#include "logging.h"
#include "skip_update.h"
#include "../resource.h"
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <mutex>
#include <map>

// External references
extern std::vector<std::pair<std::string,std::string>> g_packages;
extern std::mutex g_packages_mutex;
extern std::string g_last_winget_raw;
extern std::mutex g_last_winget_raw_mutex;

// Simple i18n loader (reads locale/<locale>.txt)
static std::string ReadFileToString(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream ss; ss << ifs.rdbuf();
    return ss.str();
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

// Helper to convert UTF-8 to wide
static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}


// Entry type used by the dialog
struct UnexcludeEntry { std::string id; std::string name; std::string reason; };

// Context stored for each dialog instance
struct DialogContext {
    HWND parent;
    HWND hList;
    std::vector<UnexcludeEntry> entries;
    std::string locale;
    bool didAny = false;
};

// Window proc for the unexclude dialog. Handles button clicks directly so
// WM_COMMAND sent via SendMessage is received here and the Cancel button works.
static LRESULT CALLBACK UnexcludeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogContext *ctx = (DialogContext*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        if (!ctx) return DefWindowProcW(hWnd, msg, wParam, lParam);
        if (id == 1002) { // Cancel
            // Bring main window to front and close the dialog
            if (ctx && ctx->parent && IsWindow(ctx->parent)) PostMessageW(ctx->parent, (WM_APP+7), 0, 0);
            DestroyWindow(hWnd);
            return 0;
        }
        if (id == 1001) { // Unexclude
            // Get selected item from list
            LRESULT sel = SendMessageW(ctx->hList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR || sel < 0 || (size_t)sel >= ctx->entries.size()) {
                std::string msg = LoadI18nValue(ctx->locale, "unexclude_select_app");
                if (msg.empty()) msg = "Please select an app to unexclude.";
                std::wstring title = Utf8ToWide(LoadI18nValue(ctx->locale, "app_title"));
                if (title.empty()) title = L"WinUpdate";
                MessageBoxW(hWnd, Utf8ToWide(msg).c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            std::string id = ctx->entries[sel].id;
            AppendLog(std::string("Unexclude: attempting to remove id=") + id + "\n");
            
            // Unexclude the app
            bool ok = false;
            try {
                UnexcludeApp(id);
                ok = true;
            } catch(...) {
                AppendLog("Unexclude: exception during UnexcludeApp\n");
            }
            
            AppendLog(std::string("Unexclude: result = ") + (ok?"true":"false") + "\n");
            if (ok) {
                ctx->didAny = true;
                // Remove from list
                SendMessageW(ctx->hList, LB_DELETESTRING, sel, 0);
                // Remove from entries vector
                ctx->entries.erase(ctx->entries.begin() + sel);
                // If list is empty, close dialog
                if (ctx->entries.empty()) {
                    std::string msg = LoadI18nValue(ctx->locale, "unexclude_list_empty");
                    if (msg.empty()) msg = "No more excluded apps. Closing dialog.";
                    std::wstring title = Utf8ToWide(LoadI18nValue(ctx->locale, "app_title"));
                    if (title.empty()) title = L"WinUpdate";
                    MessageBoxW(hWnd, Utf8ToWide(msg).c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
                    DestroyWindow(hWnd);
                }
            } else {
                std::string msg = LoadI18nValue(ctx->locale, "unexclude_failed");
                if (msg.empty()) msg = "Failed to unexclude app.";
                std::wstring title = Utf8ToWide(LoadI18nValue(ctx->locale, "app_title"));
                if (title.empty()) title = L"WinUpdate";
                MessageBoxW(hWnd, Utf8ToWide(msg).c_str(), title.c_str(), MB_OK | MB_ICONERROR);
            }
            return 0;
        }
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hWnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        // leave context alive until ShowUnexcludeDialog cleans it up
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// --- custom tooltip implementation for Unexclude dialog (file-scope) ---
static bool g_unexclude_tip_class_registered = false;
static const wchar_t *kUnexcludeTipClass = L"WUP_UnexcludeCustomTip";
static const int kUnexcludeTipPadX = 30;
static const int kUnexcludeTipPadY = 12;

static void EnsureUnexcludeTipClassRegistered() {
    if (g_unexclude_tip_class_registered) return;
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)->LRESULT{
        switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)(COLOR_INFOBK+1));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, GetSysColor(COLOR_INFOTEXT));
            HFONT hf = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
            HGDIOBJ oldf = NULL; if (hf) oldf = SelectObject(hdc, hf);
            int maxLen = 512;
            std::vector<wchar_t> buf(maxLen, 0);
            GetWindowTextW(hwnd, buf.data(), maxLen);
            RECT inner = rc; inner.left += kUnexcludeTipPadX; inner.right -= kUnexcludeTipPadX; inner.top += kUnexcludeTipPadY; inner.bottom -= kUnexcludeTipPadY;
            DrawTextW(hdc, buf.data(), -1, &inner, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
            if (oldf) SelectObject(hdc, oldf);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    };
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
    wc.lpszClassName = kUnexcludeTipClass;
    RegisterClassExW(&wc);
    g_unexclude_tip_class_registered = true;
}

static std::unordered_map<HWND, HWND> g_unexclude_tooltips;
static std::unordered_map<HWND, std::wstring> g_unexclude_texts;

static HWND EnsureUnexcludeTooltipForList(HWND hList) {
    auto it = g_unexclude_tooltips.find(hList);
    if (it != g_unexclude_tooltips.end() && IsWindow(it->second)) return it->second;
    EnsureUnexcludeTipClassRegistered();
    HWND owner = GetAncestor(hList, GA_ROOT);
    if (!owner) owner = GetParent(hList);
    HWND tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kUnexcludeTipClass, L"", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, owner, NULL, GetModuleHandleW(NULL), NULL);
    if (!tip) return NULL;
    SetWindowPos(tip, HWND_TOP, 0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    g_unexclude_tooltips[hList] = tip;
    g_unexclude_texts[hList] = L"";
    HFONT lf = (HFONT)SendMessageW(hList, WM_GETFONT, 0, 0);
    if (lf) SendMessageW(tip, WM_SETFONT, (WPARAM)lf, TRUE);
    return tip;
}

static LRESULT CALLBACK Unexclude_ListSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    static std::unordered_map<HWND, int> lastIndex;
    switch (uMsg) {
    case WM_MOUSEMOVE: {
        POINT pt; pt.x = GET_X_LPARAM(lParam); pt.y = GET_Y_LPARAM(lParam);
        // ensure we get WM_MOUSELEAVE and WM_MOUSEHOVER
        TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE | TME_HOVER; tme.hwndTrack = hwnd; TrackMouseEvent(&tme);
        // try fast compute using item height and top index
        int idx = -1;
        LRESULT count = SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
        LRESULT itemH = SendMessageW(hwnd, LB_GETITEMHEIGHT, 0, 0);
        LRESULT top = SendMessageW(hwnd, LB_GETTOPINDEX, 0, 0);
        if (itemH > 0) {
            int rel = pt.y;
            int calc = (int)top + (rel / (int)itemH);
            if (calc >= 0 && calc < (int)count) idx = calc;
        }
        if (idx == -1) {
            LPARAM lp = MAKELPARAM(pt.x, pt.y);
            LRESULT res = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lp);
            idx = LOWORD(res);
        }
        if (idx < 0) {
            auto it = g_unexclude_tooltips.find(hwnd);
            if (it != g_unexclude_tooltips.end() && IsWindow(it->second)) ShowWindow(it->second, SW_HIDE);
            lastIndex[hwnd] = -1;
            break;
        }
        if (lastIndex[hwnd] == idx) break;
        lastIndex[hwnd] = idx;
        HWND tip = EnsureUnexcludeTooltipForList(hwnd);
        if (!tip) break;
        std::wstring text = g_unexclude_texts[hwnd];
        if (text.empty()) break;
        SetWindowTextW(tip, text.c_str());
        const int padX = kUnexcludeTipPadX, padY = kUnexcludeTipPadY;
        HDC hdc = GetDC(tip);
        HFONT hf = (HFONT)SendMessageW(tip, WM_GETFONT, 0, 0);
        HGDIOBJ oldf = NULL; if (hf) oldf = SelectObject(hdc, hf);
        RECT rcCalc = {0,0,0,0}; DrawTextW(hdc, text.c_str(), -1, &rcCalc, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        int naturalW = rcCalc.right - rcCalc.left; int naturalH = rcCalc.bottom - rcCalc.top;
        if (oldf) SelectObject(hdc, oldf);
        ReleaseDC(tip, hdc);
        int w = naturalW + (padX * 2); int h = naturalH + (padY * 2);
        RECT itemRect; SendMessageW(hwnd, LB_GETITEMRECT, idx, (LPARAM)&itemRect);
        POINT tl = { itemRect.left, itemRect.top }; POINT br = { itemRect.right, itemRect.bottom };
        ClientToScreen(hwnd, &tl); ClientToScreen(hwnd, &br);
        int tipX = (tl.x + br.x) / 2 - w/2; int tipY = tl.y - h - 8;
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        if (tipX < wa.left) tipX = wa.left + 4; if (tipX + w > wa.right) tipX = wa.right - w - 4;
        if (tipY < wa.top) tipY = br.y + 8;
        SetWindowPos(tip, HWND_TOP, tipX, tipY, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(tip, NULL, TRUE);
        break;
    }
    case WM_MOUSEHOVER: {
        // Treat hover like mouse move so the tooltip appears when the mouse lingers
        SendMessageW(hwnd, WM_MOUSEMOVE, wParam, lParam);
        break;
    }
    case WM_MOUSELEAVE: {
        auto it = g_unexclude_tooltips.find(hwnd);
        if (it != g_unexclude_tooltips.end() && IsWindow(it->second)) ShowWindow(it->second, SW_HIDE);
        lastIndex[hwnd] = -1;
        break;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, Unexclude_ListSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

bool ShowUnexcludeDialog(HWND parent, const std::string &locale) {
    // Get excluded apps
    std::vector<UnexcludeEntry> entries;
    
    // Build a map from the in-memory winget output to get ALL package names (including excluded)
    std::map<std::string, std::string> idToName; // id -> display name
    try {
        std::string raw;
        {
            std::lock_guard<std::mutex> lk(g_last_winget_raw_mutex);
            raw = g_last_winget_raw;
        }
        if (!raw.empty()) {
            AppendLog("[unexclude] Parsing in-memory winget output for display names\n");
            std::istringstream iss(raw);
            std::string line;
            bool pastHeader = false;
            while (std::getline(iss, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                if (!pastHeader) {
                    if (line.find("----") != std::string::npos) { pastHeader = true; }
                    continue;
                }
                if (line.find("upgrades available") != std::string::npos) break;
                if (line.empty()) continue;
                
                // Split into tokens
                std::istringstream ls(line);
                std::vector<std::string> tokens;
                std::string tok;
                while (ls >> tok) tokens.push_back(tok);
                
                if (tokens.size() < 5) continue;
                
                size_t n = tokens.size();
                // Right to left: Source, Available, Version, Id, then Name is everything else
                std::string id = tokens[n-4];
                std::string name;
                for (size_t i = 0; i + 4 < n; ++i) {
                    if (i > 0) name += " ";
                    name += tokens[i];
                }
                if (!name.empty() && !id.empty()) {
                    idToName[id] = name;
                    AppendLog(std::string("[unexclude] In-memory: id='") + id + "' name='" + name + "'\n");
                }
            }
        }
    } catch(...) {}
    
    WaitForSingleObject(g_excluded_mutex, INFINITE);
    for (const auto &kv : g_excluded_apps) {
        UnexcludeEntry e;
        // Trim the key (id might have trailing spaces from INI parsing)
        e.id = kv.first;
        auto trim = [](std::string s){ size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) return std::string(); size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1); };
        e.id = trim(e.id);
        e.reason = kv.second;
        
        // Try memory first
        e.name = GetDisplayNameForId(e.id);
        // If still same as id, try in-memory winget output
        if (e.name == e.id) {
            auto it = idToName.find(e.id);
            if (it != idToName.end()) {
                e.name = it->second;
                AppendLog(std::string("[unexclude] Using in-memory name='") + e.name + "' for id='" + e.id + "'\n");
            }
        }
        // If still same as id, try g_packages
        if (e.name == e.id) {
            try {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                for (auto &p : g_packages) {
                    if (p.first == e.id) { e.name = p.second; break; }
                }
            } catch(...) {}
        }
        entries.push_back(e);
    }
    ReleaseMutex(g_excluded_mutex);
    
    // Helper to prettify an id into a readable name if no display name found
    auto PrettyNameFromId = [](const std::string &id)->std::string{
        if (id.empty()) return id;
        // if id contains dots, take last token
        size_t p = id.find_last_of('.');
        std::string base = (p==std::string::npos) ? id : id.substr(p+1);
        // split on camelCase or capitals
        std::string out;
        for (size_t i = 0; i < base.size(); ++i) {
            char c = base[i];
            if (i>0 && isupper((unsigned char)c) && (islower((unsigned char)base[i-1]) || (i+1<base.size() && islower((unsigned char)base[i+1])))) out.push_back(' ');
            if (c == '_' || c == '-') out.push_back(' ');
            else out.push_back(c);
        }
        // capitalize first letter
        if (!out.empty()) out[0] = (char)toupper((unsigned char)out[0]);
        return out;
    };
    
    // If still unknown display names, prettify from id
    for (auto &e : entries) {
        if (e.name == e.id) e.name = PrettyNameFromId(e.id);
    }
    
    // If no excluded apps, show message and return
    if (entries.empty()) {
        std::string msg = LoadI18nValue(locale, "no_excluded_apps"); 
        if (msg.empty()) msg = "No apps are currently excluded.";
        MessageBoxW(parent, Utf8ToWide(msg).c_str(), L"WinUpdate", MB_OK | MB_ICONINFORMATION);
        return false;
    }

    // create simple modal dialog
    static const wchar_t *kClass = L"WUP_UnexcludeDlg";
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = UnexcludeWndProc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = kClass; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
        RegisterClassExW(&wc); reg = true;
    }
    int W = 520, H = 360;
    RECT prc{}; int px=100, py=100;
    if (parent && IsWindow(parent)) { GetWindowRect(parent, &prc); px = prc.left + 40; py = prc.top + 40; }
    std::wstring title = Utf8ToWide(LoadI18nValue(locale, "unexclude_dialog_title"));
    if (title.empty()) title = L"Manage Excluded Apps";
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, title.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, px, py, W, H, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!hDlg) return false;
    
    // Set app icon
    HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (hIcon) {
        SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    }
    // allocate context for this dialog and copy entries
    DialogContext *ctx = new DialogContext();
    ctx->parent = parent;
    ctx->entries = entries;
    ctx->locale = locale;
    // create listbox
    // adjust dialog height to fit entries (but clamp to reasonable sizes)
    int itemH = 20;
    int listH = std::max(80, std::min((int)entries.size() * (itemH + 2) + 8, 360));
    int dlgH = 40 + listH + 80; // top padding + list + buttons area
    if (dlgH > 600) dlgH = 600;
    H = dlgH;
    // Resize the dialog window so it matches the calculated height
    if (hDlg && IsWindow(hDlg)) {
        SetWindowPos(hDlg, NULL, 0, 0, W, H, SWP_NOMOVE | SWP_NOZORDER);
    }
    HWND hList = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 12, 12, W-24, listH, hDlg, NULL, GetModuleHandleW(NULL), NULL);
    // fill listbox: show display name and reason (like unskip shows name and version)
    for (size_t i = 0; i < entries.size(); ++i) {
        // Translate reason
        std::string reasonLabel;
        if (entries[i].reason == "auto") {
            reasonLabel = LoadI18nValue(locale, "excluded_reason_auto");
            if (reasonLabel.empty()) reasonLabel = "auto";
        } else if (entries[i].reason == "manual") {
            reasonLabel = LoadI18nValue(locale, "excluded_reason_manual");
            if (reasonLabel.empty()) reasonLabel = "manual";
        } else {
            reasonLabel = entries[i].reason;
        }
        std::string line = entries[i].name + "  -  " + reasonLabel;
        std::wstring wl;
        int needed = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, NULL, 0);
        if (needed>0) { std::vector<wchar_t> buf(needed); MultiByteToWideChar(CP_UTF8,0,line.c_str(),-1,buf.data(),needed); wl = buf.data(); }
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)wl.c_str());
    }
    // store list HWND in context and associate context with dialog
    ctx->hList = hList;
    SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)ctx);
    // buttons: Unexclude and Cancel
    std::string txtUn = LoadI18nValue(locale, "unexclude"); if (txtUn.empty()) txtUn = "Unexclude";
    std::string txtCancel = LoadI18nValue(locale, "btn_cancel"); if (txtCancel.empty()) txtCancel = "Cancel";
    std::wstring wun = Utf8ToWide(txtUn);
    std::wstring wcancel = Utf8ToWide(txtCancel);
    HWND hBtnUn = CreateWindowExW(0, L"BUTTON", wun.c_str(), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, W-240, H-88, 130, 30, hDlg, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", wcancel.c_str(), WS_CHILD | WS_VISIBLE, W-100, H-88, 88, 30, hDlg, (HMENU)1002, GetModuleHandleW(NULL), NULL);

    // ensure common controls used by subclassing
    INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icce);
    // set the tooltip text into the map for this list
    std::string tipText = LoadI18nValue(locale, "unexclude_tooltip"); if (tipText.empty()) tipText = "Click the row and press Unexclude to remove the app from the exclusion list";
    std::wstring wtip = Utf8ToWide(tipText);
    HWND preTip = EnsureUnexcludeTooltipForList(hList);
    g_unexclude_texts[hList] = wtip;
    // subclass the list to show the custom tooltip on hover
    SetWindowSubclass(hList, Unexclude_ListSubclassProc, 0xBEEFDEAD, 0);

    ShowWindow(hDlg, SW_SHOW);
    // modal message loop for this dialog
    bool didAny = false;
    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, NULL, 0, 0)) {
        // let dialog manager process dialog-specific keys first
        if (IsDialogMessage(hDlg, &msg)) continue;
        // WM_COMMANDs are handled in the dialog window procedure (UnexcludeWndProc)
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    // retrieve didAny from context and cleanup
    if (ctx) {
        didAny = ctx->didAny;
        delete ctx;
        ctx = nullptr;
    }
    return didAny;
}
