#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "../resource.h"
#include <string>
#include <vector>
#include <sstream>
#include <set>
#include <regex>
#include <fstream>
#include <thread>
#include <mutex>
#include <filesystem>
#include <unordered_map>
#include <future>
#include <filesystem>
// detect nlohmann/json.hpp if available; fall back to ad-hoc parser otherwise
#if defined(__has_include)
#  if __has_include(<nlohmann/json.hpp>)
#    include <nlohmann/json.hpp>
#    define HAVE_NLOHMANN_JSON 1
#  else
#    define HAVE_NLOHMANN_JSON 0
#  endif
#else
#  define HAVE_NLOHMANN_JSON 0
#endif

const wchar_t CLASS_NAME[] = L"WinUpdateClass";

// control IDs
#define IDC_RADIO_SHOW 1001
#define IDC_RADIO_ALL  1002
#define IDC_BTN_REFRESH 1003
#define IDC_LISTVIEW 1004
#define IDC_CHECK_SELECTALL 2001
#define IDC_BTN_UPGRADE 2002
#define IDC_COMBO_LANG 3001
#define WM_REFRESH_ASYNC (WM_APP + 1)
#define WM_REFRESH_DONE  (WM_APP + 2)

// Forward declarations for functions defined later in the file
static void ParseWingetTextForPackages(const std::string &text);
static std::vector<std::pair<std::string,std::string>> ExtractIdsFromNameIdText(const std::string &text);

// Globals
static std::vector<std::pair<std::string,std::string>> g_packages;
static std::mutex g_packages_mutex;
static std::set<std::string> g_not_applicable_ids;
static HWND g_hTitle = NULL;
static HWND g_hLastUpdated = NULL;
static HFONT g_hTitleFont = NULL;
static HFONT g_hLastUpdatedFont = NULL;
static HWND g_hLoadingPopup = NULL;
static HWND g_hLoadingIcon = NULL;
static HWND g_hLoadingText = NULL;
static HWND g_hLoadingDesc = NULL;
static HWND g_hLoadingDots = NULL;
static HFONT g_hDotsFont = NULL;
static HWND g_hMainWindow = NULL;
static int g_loading_anim_state = 0;
static const UINT LOADING_TIMER_ID = 0xC0DE;
static bool g_popupClassRegistered = false;

// Simple i18n: UTF-8 key=value loader. Keys are ASCII.
static std::unordered_map<std::string,std::string> g_i18n_default;
static std::unordered_map<std::string,std::string> g_i18n;
static std::string g_locale = "en";

// forward declare helper functions used by i18n loader (defined later)
static std::string ReadFileUtf8(const std::wstring &path);
static std::wstring Utf8ToWide(const std::string &s);

static void InitDefaultTranslations() {
    if (!g_i18n_default.empty()) return;
    g_i18n_default["app_window_title"] = "WinUpdate - winget GUI updater";
    g_i18n_default["app_title"] = "WinUpdate";
    g_i18n_default["list_last_updated_prefix"] = "List last updated:";
    g_i18n_default["select_all"] = "Select all";
    g_i18n_default["upgrade_now"] = "Upgrade now";
    g_i18n_default["refresh"] = "Refresh";
    g_i18n_default["package_col"] = "Package";
    g_i18n_default["id_col"] = "Id";
    g_i18n_default["loading_title"] = "Loading, please";
    g_i18n_default["loading_desc"] = "Querying winget";
    g_i18n_default["your_system_updated"] = "Your system is updated!";
    g_i18n_default["msg_error_elevate"] = "Failed to launch elevated process.";
}

static void LoadLocaleFromFile(const std::string &locale) {
    g_i18n = g_i18n_default; // start with defaults
    std::string path = std::string("i18n\\") + locale + ".txt";
    std::string txt = ReadFileUtf8(std::wstring(path.begin(), path.end()));
    if (txt.empty()) return;
    std::istringstream iss(txt);
    std::string ln;
    while (std::getline(iss, ln)) {
        // Trim
        auto ltrim = [](std::string &s){ while(!s.empty() && (s.front()==' '||s.front()=='\t' || s.front()=='\r')) s.erase(s.begin()); };
        auto rtrim = [](std::string &s){ while(!s.empty() && (s.back()==' '||s.back()=='\t' || s.back()=='\r' || s.back()=='\n')) s.pop_back(); };
        ltrim(ln); rtrim(ln);
        if (ln.empty()) continue;
        if (ln[0] == '#' || ln[0] == ';') continue;
        size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        std::string key = ln.substr(0, eq);
        std::string val = ln.substr(eq+1);
        ltrim(key); rtrim(key); ltrim(val); rtrim(val);
        if (!key.empty()) g_i18n[key] = val;
    }
}

static std::wstring t(const char *key) {
    InitDefaultTranslations();
    std::string k(key);
    auto it = g_i18n.find(k);
    if (it == g_i18n.end()) it = g_i18n_default.find(k);
    if (it == g_i18n_default.end()) return Utf8ToWide(k);
    return Utf8ToWide(it->second);
}

// Settings persistence: simple UTF-8 key=value in wup_settings.txt
static bool SaveLocaleSetting(const std::string &locale) {
    try {
        std::string fn = "wup_settings.txt";
        std::ofstream ofs(fn, std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs << "language=" << locale << "\n";
        return true;
    } catch(...) { return false; }
}

static std::string LoadLocaleSetting() {
    try {
        std::string fn = "wup_settings.txt";
        std::ifstream ifs(fn, std::ios::binary);
        if (!ifs) return std::string();
        std::ostringstream ss; ss << ifs.rdbuf();
        std::string txt = ss.str();
        std::istringstream iss(txt);
        std::string ln;
        while (std::getline(iss, ln)) {
            // trim
            auto ltrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); };
            auto rtrim = [](std::string &s){ while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); };
            ltrim(ln); rtrim(ln);
            if (ln.empty()) continue;
            if (ln[0] == '#' || ln[0] == ';') continue;
            size_t eq = ln.find('=');
            if (eq == std::string::npos) continue;
            std::string key = ln.substr(0, eq);
            std::string val = ln.substr(eq+1);
            ltrim(key); rtrim(key); ltrim(val); rtrim(val);
            if (key == "language" && !val.empty()) return val;
        }
    } catch(...) {}
    return std::string();
}

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
// subclass procedure for custom-drawn dots control
static LRESULT CALLBACK DotsSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        // fill background with dialog face
        HBRUSH hbr = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &rc, hbr);
        // determine number of dots from global animation state
        int state = g_loading_anim_state % 3;
        int count = (state == 0) ? 1 : (state == 1) ? 3 : 5;
        // compute diameter and spacing (smaller, more compact)
        int ch = rc.bottom - rc.top;
        int dia = std::min(12, std::max(6, ch - 12));
        int gap = dia / 2; // tighter spacing
        int totalW = count * dia + (count - 1) * gap;
        int startX = rc.left + ((rc.right - rc.left) - totalW) / 2;
        int y = rc.top + (rc.bottom - rc.top - dia) / 2;
        // paint filled navy circles (select pen+brush)
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0,0,128));
        HBRUSH hBrush = CreateSolidBrush(RGB(0,0,128));
        HGDIOBJ oldPen = SelectObject(hdc, hPen);
        HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
        for (int i = 0; i < count; ++i) {
            int x = startX + i * (dia + gap);
            Ellipse(hdc, x, y, x + dia, y + dia);
        }
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(hPen);
        DeleteObject(hBrush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        RemoveWindowSubclass(hwnd, DotsSubclassProc, uIdSubclass);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}
static void EnsurePopupClassRegistered(HINSTANCE hInst) {
    if (g_popupClassRegistered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PopupWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"WUPopupClass";
    RegisterClassExW(&wc);
    g_popupClassRegistered = true;
}

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // start animation timer on popup (175ms per user preference)
        g_loading_anim_state = 0;
        SetTimer(hwnd, LOADING_TIMER_ID, 175, NULL);
        return 0;
    }
    case WM_TIMER: {
        if (wParam == LOADING_TIMER_ID) {
            g_loading_anim_state = (g_loading_anim_state + 1) % 3; // cycle 0..2
            InvalidateRect(hwnd, NULL, TRUE); // full repaint (double-buffered)
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // double-buffering
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);

        // fill background with dialog face color
        HBRUSH hbrFace = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdcMem, &rc, hbrFace);

        // draw a simple frame to mimic dialog border
        HBRUSH hbrFrame = GetSysColorBrush(COLOR_WINDOWFRAME);
        RECT fr = rc;
        FrameRect(hdcMem, &fr, hbrFrame);

        // layout: left icon cell, right title/desc cell (row1); row2 merged for dots
        int padding = 12;
        int iconW = 48; int iconH = 48;
        int ix = padding;
        int iy = padding;
        // draw information icon using standard system icon
        HICON hInfo = LoadIcon(NULL, IDI_INFORMATION);
        if (hInfo) DrawIconEx(hdcMem, ix, iy, hInfo, iconW, iconH, 0, NULL, DI_NORMAL);

        int txtX = ix + iconW + padding;
        int txtW = w - txtX - padding;
        int row1Height = iconH + padding; // provide vertical space for icon + text
        // Title (big)
        RECT titleRect = { txtX, iy, txtX + txtW, iy + row1Height / 2 };
        // Description under title within first row
        RECT descRect = { txtX, iy + row1Height / 2, txtX + txtW, iy + row1Height };

        // draw title using existing title font if available (preserves ClearType)
        if (g_hTitleFont) {
            HGDIOBJ oldFont = SelectObject(hdcMem, g_hTitleFont);
            SetTextColor(hdcMem, RGB(0,0,0));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_title").c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            SelectObject(hdcMem, oldFont);
        } else {
            SetTextColor(hdcMem, RGB(0,0,0));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_title").c_str(), -1, &titleRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        // draw description smaller
        if (g_hLastUpdatedFont) {
            HGDIOBJ oldFont = SelectObject(hdcMem, g_hLastUpdatedFont);
            SetTextColor(hdcMem, RGB(64,64,64));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_desc").c_str(), -1, &descRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            SelectObject(hdcMem, oldFont);
        } else {
            SetTextColor(hdcMem, RGB(64,64,64));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, t("loading_desc").c_str(), -1, &descRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        // Row2: center dots across full width (merged cell)
        int row2Top = iy + row1Height + padding/2;
        int row2H = h - row2Top - padding;
        RECT dotsArea = { rc.left + padding, row2Top, rc.right - padding, row2Top + row2H };

        int state = g_loading_anim_state % 3;
        int count = (state == 0) ? 1 : (state == 1) ? 3 : 5;
        int dia = std::min(14, std::max(6, row2H - 8));
        int gap = dia / 2;
        int totalW = count * dia + (count - 1) * gap;
        int centerX = (dotsArea.left + dotsArea.right) / 2;
        int startX = centerX - totalW / 2;
        int y = dotsArea.top + (row2H - dia) / 2;

        // draw navy filled circles
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0,0,128));
        HBRUSH hBrush = CreateSolidBrush(RGB(0,0,128));
        HGDIOBJ oldPen = SelectObject(hdcMem, hPen);
        HGDIOBJ oldBrush = SelectObject(hdcMem, hBrush);
        for (int i = 0; i < count; ++i) {
            int x = startX + i * (dia + gap);
            Ellipse(hdcMem, x, y, x + dia, y + dia);
        }
        SelectObject(hdcMem, oldPen);
        SelectObject(hdcMem, oldBrush);
        DeleteObject(hPen);
        DeleteObject(hBrush);

        // blit buffer to screen
        BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

        // cleanup
        SelectObject(hdcMem, oldBmp);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, LOADING_TIMER_ID);
        return 0;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}


// Run a command, capture stdout/stderr to a temp file, return exit code and UTF-8 output.
static std::pair<int,std::string> RunProcessCaptureExitCode(const std::wstring &cmd, int timeoutMs) {
    std::wstring command = cmd;
    wchar_t tmpPathBuf[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPathBuf);
    unsigned long long uniq = GetTickCount64();
    std::wstring batch = std::wstring(tmpPathBuf) + L"wup_run_" + std::to_wstring(uniq) + L".bat";
    std::wstring outfn = std::wstring(tmpPathBuf) + L"wup_out_" + std::to_wstring(uniq) + L".txt";

    auto toUtf8 = [](const std::wstring &w)->std::string {
        if (w.empty()) return std::string();
        int sz = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
        if (sz <= 0) return std::string();
        std::string out(sz, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], sz, NULL, NULL);
        return out;
    };

    std::string nbatch = toUtf8(batch);
    std::string nout = toUtf8(outfn);
    std::string ncmd = toUtf8(command);

    try {
        std::ofstream ofs(nbatch, std::ios::binary);
        if (ofs) {
            ofs << "@echo off\r\n";
            ofs << ncmd << " > \"" << nout << "\" 2>&1\r\n";
            ofs.close();
        }
    } catch(...) {}

    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring runCmd = L"cmd.exe /C \"" + batch + L"\"";
    BOOL ok = CreateProcessW(NULL, &runCmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    DWORD exitCode = 0;
    std::string output;
    if (!ok) {
        try { std::ifstream ifs(nout, std::ios::binary); if (ifs) { std::ostringstream ss; ss << ifs.rdbuf(); output = ss.str(); } } catch(...) {}
        DeleteFileW(batch.c_str()); DeleteFileW(outfn.c_str());
        return { -1, output };
    }
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs > 0 ? (DWORD)timeoutMs : INFINITE);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        exitCode = (DWORD)-2;
    } else {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    try { std::ifstream ifs(nout, std::ios::binary); if (ifs) { std::ostringstream ss; ss << ifs.rdbuf(); output = ss.str(); } } catch(...) {}
    DeleteFileW(batch.c_str()); DeleteFileW(outfn.c_str());

    try {
        std::ofstream lof("wup_run_log.txt", std::ios::app | std::ios::binary);
        if (lof) {
            lof << "--- CMD: " << ncmd << " ---\n";
            lof << "Exit: " << (int)exitCode << "\n";
            if (exitCode == (DWORD)-2) lof << "(TIMEOUT)\n";
            lof << "Output:\n" << output << "\n\n";
        }
    } catch(...) {}

    return { (int)exitCode, output };
}

static std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

static std::string ReadFileUtf8(const std::wstring &path) {
    std::string narrow = WideToUtf8(path);
    std::ifstream ifs(narrow, std::ios::binary);
    if (!ifs) return std::string();
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::wstring GetTimestampNow() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, _countof(buf), L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

static void UpdateLastUpdatedLabel(HWND hwnd) {
    if (!g_hLastUpdated) return;
    std::wstring ts = GetTimestampNow();
    std::wstring prefix = t("list_last_updated_prefix");
    std::wstring txt = prefix + L" " + ts;
    SetWindowTextW(g_hLastUpdated, txt.c_str());
}

static void ShowLoading(HWND parent) {
    if (!parent) return;
    if (g_hLoadingPopup && IsWindow(g_hLoadingPopup)) return;
    RECT rc;
    GetWindowRect(parent, &rc);
    int pw = rc.right - rc.left;
    int ph = rc.bottom - rc.top;
    int w = 260; int h = 110;
    int x = rc.left + (pw - w) / 2;
    int y = rc.top + (ph - h) / 2;
    // Create a popup window that looks like an informational dialog
    // Create an owned border-style popup (no title or close button)
    HINSTANCE hInst = GetModuleHandleW(NULL);
    // ensure our popup window class is registered and create a top-level border popup
    EnsurePopupClassRegistered(hInst);
    // create a top-level border popup (no caption/title bar) positioned centered over parent
    g_hLoadingPopup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"WUPopupClass", NULL,
        WS_POPUP | WS_BORDER | WS_VISIBLE, x, y, w, h, NULL, NULL, hInst, NULL);
    if (g_hLoadingPopup) {
        // Owner-drawn popup: the window procedure paints icon, title/desc and centered dots.
        ShowWindow(g_hLoadingPopup, SW_SHOW);
        UpdateWindow(g_hLoadingPopup);
        // WM_CREATE handler of the popup will start the 175ms timer.
    }
}

static void HideLoading() {
    if (g_hLoadingPopup && IsWindow(g_hLoadingPopup)) {
        DestroyWindow(g_hLoadingPopup);
        g_hLoadingPopup = NULL;
        g_hLoadingIcon = NULL;
        g_hLoadingText = NULL;
        g_hLoadingDesc = NULL;
        g_hLoadingDots = NULL;
    }
}

// Query the system for the installed version of a package by id using `winget list`.
// (No per-id installed-version helper in this restore)

static std::string RunWingetElevatedCaptureJson(HWND hwnd) {
    wchar_t tmpPathBuf[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpPathBuf);
    unsigned long long uniq = GetTickCount64();
    std::wstring batch = std::wstring(tmpPathBuf) + L"winget_run_" + std::to_wstring(uniq) + L".bat";
    std::wstring outfn = std::wstring(tmpPathBuf) + L"winget_out_" + std::to_wstring(uniq) + L".txt";
    // write batch that redirects output to outfn
    std::string nbatch = WideToUtf8(batch);
    std::string nout = WideToUtf8(outfn);
    std::ofstream ofs(nbatch, std::ios::binary);
    ofs << "@echo off\r\n";
    ofs << "winget upgrade --accept-source-agreements --accept-package-agreements > \"" << nout << "\" 2>&1\r\n";
    ofs.close();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hwnd;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    std::wstring params = L"/C \"" + batch + L"\"";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE; // hide elevated console window
    if (!ShellExecuteExW(&sei)) {
        return std::string();
    }
    DWORD wait = WaitForSingleObject(sei.hProcess, 15000); // 15s timeout for elevated capture
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(sei.hProcess, 1);
    }
    CloseHandle(sei.hProcess);
    std::string out = ReadFileUtf8(outfn);
    // cleanup temp files
    DeleteFileW(batch.c_str());
    DeleteFileW(outfn.c_str());
    return out;
}

// very small JSON-ish extractor for "Id" and "Name" values from winget --output json
static void ParseWingetJsonForPackages(const std::string &jsonText) {
    g_packages.clear();
#if HAVE_NLOHMANN_JSON
    if (jsonText.empty()) return;
    try {
        auto j = nlohmann::json::parse(jsonText);
        std::set<std::pair<std::string,std::string>> found;
        std::function<void(const nlohmann::json&)> visit;
        visit = [&](const nlohmann::json &node) {
            if (node.is_object()) {
                if (node.contains("Id") && node.contains("Name") && node["Id"].is_string() && node["Name"].is_string()) {
                    std::string id = node["Id"].get<std::string>();
                    std::string name = node["Name"].get<std::string>();
                    if (!id.empty()) found.emplace(id, name);
                }
                for (auto it = node.begin(); it != node.end(); ++it) visit(it.value());
            } else if (node.is_array()) {
                for (auto &el : node) visit(el);
            }
        };
        visit(j);
        for (auto &p : found) g_packages.emplace_back(p.first, p.second);
    } catch (const std::exception &e) {
        (void)e;
    }
#else
    // Fallback: simple string-based extractor (original implementation)
    size_t pos = 0;
    std::string name, id;
    while (true) {
        size_t npos = jsonText.find("\"Name\"", pos);
        size_t ipos = jsonText.find("\"Id\"", pos);
        if (npos == std::string::npos && ipos == std::string::npos) break;

        if (ipos != std::string::npos) {
            size_t colon = jsonText.find(':', ipos);
            if (colon != std::string::npos) {
                size_t q1 = jsonText.find('"', colon);
                if (q1 != std::string::npos) {
                    size_t q2 = jsonText.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        id = jsonText.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
            }
            pos = ipos + 1;
        }

        if (npos != std::string::npos) {
            size_t colon = jsonText.find(':', npos);
            if (colon != std::string::npos) {
                size_t q1 = jsonText.find('"', colon);
                if (q1 != std::string::npos) {
                    size_t q2 = jsonText.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        name = jsonText.substr(q1 + 1, q2 - q1 - 1);
                        if (!id.empty()) {
                            g_packages.emplace_back(id, name);
                            id.clear();
                            name.clear();
                        }
                    }
                }
            }
            pos = npos + 1;
        }
    }
#endif
}

// Parse human-readable `winget upgrade` text output.
// compare semantic-ish version strings: returns -1 if a<b, 0 if equal, 1 if a>b
static int CompareVersions(const std::string &a, const std::string &b) {
    if (a == b) return 0;
    std::istringstream sa(a), sb(b);
    std::string ta, tb;
    while (true) {
        if (!std::getline(sa, ta, '.')) ta.clear();
        if (!std::getline(sb, tb, '.')) tb.clear();
        if (ta.empty() && tb.empty()) break;
        // try numeric compare
        long va = 0, vb = 0;
        try { va = std::stol(ta.empty()?"0":ta); } catch(...) { va = 0; }
        try { vb = std::stol(tb.empty()?"0":tb); } catch(...) { vb = 0; }
        if (va < vb) return -1;
        if (va > vb) return 1;
        // continue
        if (!sa.good() && !sb.good()) break;
    }
    return 0;
}

// Parse text output and pick only entries where an available version is greater
static void ParseWingetTextForUpdates(const std::string &text) {
    g_packages.clear();
    std::istringstream iss(text);
    std::string line;
    // match lines that end with: <installed-version> <available-version>
    std::regex lineRe("^\\s*(.+?)\\s+([^\\s]+)\\s+(\\d+(?:\\.\\d+)*)\\s+(\\d+(?:\\.\\d+)*)\\s*$");
    std::smatch m;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        if (line.empty()) continue;
        if (std::regex_match(line, m, lineRe)) {
            // m[1]=name, m[2]=id, m[3]=installed, m[4]=available
            std::string name = m[1].str();
            std::string id = m[2].str();
            std::string installed = m[3].str();
            std::string available = m[4].str();
            if (CompareVersions(installed, available) < 0) {
                g_packages.emplace_back(id, name);
            }
        }
    }
}

// Very fast upgrade output parser: split each non-header line into tokens,
// take the last tokens as id/installed/available and compare versions.
static void ParseUpgradeFast(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
    std::istringstream iss(text);
    std::string line;
    bool seenHeader = false;
    std::regex verRe(R"(^[0-9]+(\.[0-9]+)*$)");
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        std::string t = trim(line);
        if (t.empty()) continue;
        if (!seenHeader) {
            if (t.find("Name") != std::string::npos && t.find("Id") != std::string::npos) { seenHeader = true; continue; }
            continue;
        }
        if (t.find("----") != std::string::npos) continue;
        if (t.find("upgrades available") != std::string::npos) break;

        std::istringstream ls(t);
        std::vector<std::string> toks;
        std::string tok;
        while (ls >> tok) toks.push_back(tok);
        if (toks.size() < 3) continue;

        int n = (int)toks.size();
        int verIdx2 = -1, verIdx1 = -1;
        for (int i = n - 1; i >= 1; --i) {
            if (std::regex_match(toks[i], verRe) && std::regex_match(toks[i-1], verRe)) { verIdx2 = i; verIdx1 = i-1; break; }
        }
        if (verIdx1 < 0) continue;
        int idIdx = verIdx1 - 1;
        if (idIdx < 0) continue;

        std::string available = toks[verIdx2];
        std::string installed = toks[verIdx1];

        auto looks_like_id = [&](const std::string &s)->bool {
            if (s.find('.') != std::string::npos) return true;
            if (s.size() >= 4) return true;
            for (char c : s) if (isupper((unsigned char)c)) return true;
            return false;
        };

        std::string id = toks[idIdx];
        if (!looks_like_id(id)) {
            int better = -1;
            for (int k = idIdx - 1; k >= 0; --k) { if (looks_like_id(toks[k])) { better = k; break; } }
            if (better >= 0) idIdx = better;
        }

        std::string name;
        for (int i = 0; i < idIdx; ++i) { if (i) name += " "; name += toks[i]; }
        if (name.empty()) name = toks[idIdx];

        id = toks[idIdx];
        if (CompareVersions(installed, available) < 0) outSet.emplace(id, name);
    }
}

// More tolerant extractor: find any occurrences of lines or fragments that contain
// <name> <id> <installed-version> <available-version> and add when available>installed
static void ExtractUpdatesFromText(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
    // regex: capture name (greedy), id token (no spaces), installed(ver), available(ver)
    std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\d+(?:\\.\\d+)*)\\s+(\\d+(?:\\.\\d+)*)");
    std::smatch m;
    std::string::const_iterator it = text.begin();
    while (std::regex_search(it, text.cend(), m, anyRe)) {
        std::string name = m[1].str();
        std::string id = m[2].str();
        std::string installed = m[3].str();
        std::string available = m[4].str();
        // trim name
        auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
        name = trim(name);
        if (!id.empty() && CompareVersions(installed, available) < 0) outSet.emplace(id, name);
        it = m.suffix().first;
    }
}

// Build a map of Id->Name from a full winget listing (Name/Id table)
// then scan the upgrade output for <name> <id> <installed> <available>
// and add entries where available > installed and id exists in the map.
static void FindUpdatesUsingKnownList(const std::string &listText, const std::string &upgradeText, std::set<std::pair<std::string,std::string>> &outSet) {
    // populate g_packages from the listText
    {
        std::lock_guard<std::mutex> lk(g_packages_mutex);
        ParseWingetTextForPackages(listText);
        // copy into local map
    }
    std::unordered_map<std::string,std::string> pkgmap;
    for (auto &p : g_packages) pkgmap[p.first] = p.second;
    // if we didn't get a useful map from the provided list, try extracting Id/Name pairs from the upgrade text
    if (pkgmap.empty() && !upgradeText.empty()) {
        auto extra = ExtractIdsFromNameIdText(upgradeText);
        for (auto &p : extra) pkgmap[p.first] = p.second;
    }
    // clear global helper list to avoid side-effects
    {
        std::lock_guard<std::mutex> lk(g_packages_mutex);
        g_packages.clear();
    }
    if (pkgmap.empty()) return;

    // regex to find fragments like: <name> <id> <installed> <available>
    std::regex anyRe(R"(([\S ]+?)\s+([^\s]+)\s+(\d+(?:\.[0-9]+)*)\s+(\d+(?:\.[0-9]+)*))");
    std::smatch m;
    std::string::const_iterator it = upgradeText.begin();
    while (std::regex_search(it, upgradeText.cend(), m, anyRe)) {
        std::string id = m[2].str();
        std::string installed = m[3].str();
        std::string available = m[4].str();
        if (!id.empty() && pkgmap.count(id) && CompareVersions(installed, available) < 0) {
            outSet.emplace(id, pkgmap[id]);
        }
        it = m.suffix().first;
    }
}

static std::vector<std::pair<std::string,std::string>> ExtractIdsFromNameIdText(const std::string &text) {
    std::vector<std::pair<std::string,std::string>> ids;
    std::istringstream iss(text);
    std::string ln;
    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
    while (std::getline(iss, ln)) {
        std::string t = trim(ln);
        if (t.empty()) continue;
        // skip header/separator lines that contain dashes or 'Name' header
        if (t.find("----") != std::string::npos) continue;
        if (t.find("Name") != std::string::npos && t.find("Id") != std::string::npos) continue;
        std::istringstream ls(t);
        std::vector<std::string> toks;
        std::string tok;
        while (ls >> tok) toks.push_back(tok);
        if (toks.size() >= 2) {
            std::string id = toks.back();
            std::string name;
            for (size_t i = 0; i + 1 < toks.size(); ++i) {
                if (i) name += " ";
                name += toks[i];
            }
            ids.emplace_back(id, name);
        }
    }
    return ids;
}

static void CheckIdsForUpdates(const std::vector<std::pair<std::string,std::string>> &candidates, std::set<std::pair<std::string,std::string>> &outFound, HWND hwnd) {
    // Probe all candidate ids in parallel with limited concurrency to keep it fast.
    unsigned int hw = std::thread::hardware_concurrency();
    size_t concurrency = hw > 0 ? std::min<unsigned int>(hw, 8) : 4;
    std::vector<std::future<std::pair<std::string,std::string>>> futures;
    futures.reserve(candidates.size());

    auto probeOne = [](const std::pair<std::string,std::string> &p)->std::pair<std::string,std::string> {
        std::wstring idw(p.first.begin(), p.first.end());
        std::wstring cmd = L"cmd /C winget upgrade --id \"" + idw + L"\" --accept-source-agreements --accept-package-agreements";
        auto res = RunProcessCaptureExitCode(cmd, 4000);
        std::string out = res.second;
        if (out.empty()) {
            res = RunProcessCaptureExitCode(cmd, 8000);
            out = res.second;
        }
        if (!out.empty()) {
            std::set<std::pair<std::string,std::string>> found;
            ExtractUpdatesFromText(out, found);
            for (auto &f : found) {
                if (f.first == p.first) return f; // return matching id/name
            }
        }
        return std::pair<std::string,std::string>();
    };

    size_t idx = 0;
    while (idx < candidates.size()) {
        // launch up to concurrency tasks
        size_t launched = 0;
        std::vector<std::future<std::pair<std::string,std::string>>> batch;
        for (; idx < candidates.size() && launched < concurrency; ++idx, ++launched) {
            batch.push_back(std::async(std::launch::async, probeOne, candidates[idx]));
        }
        // collect results
        for (auto &f : batch) {
            try {
                auto r = f.get();
                if (!r.first.empty()) outFound.emplace(r.first, r.second);
            } catch(...) {}
        }
        // small pause to avoid hammering winget
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Read the most recent raw winget output file matching prefix wup_winget_raw_*.txt
static std::string ReadMostRecentRawWinget() {
    namespace fs = std::filesystem;
    std::string best;
    std::filesystem::file_time_type bestTime = std::filesystem::file_time_type::min();
    try {
        for (auto &p : fs::directory_iterator(fs::current_path())) {
            std::string name = p.path().filename().string();
            if (name.rfind("wup_winget_raw_", 0) == 0 && p.path().extension() == ".txt") {
                auto ftime = fs::last_write_time(p.path());
                if (ftime > bestTime) {
                    bestTime = ftime;
                    best = p.path().string();
                }
            }
        }
        if (!best.empty()) {
            // read file
            std::ifstream ifs(best, std::ios::binary);
            if (ifs) {
                std::ostringstream ss; ss << ifs.rdbuf();
                return ss.str();
            }
        }
    } catch(...) {}
    return std::string();
}

static void ParseWingetTextForPackages(const std::string &text) {
    g_packages.clear();
    std::istringstream iss(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(iss, line)) {
        // trim CR
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        // keep even empty lines (we'll skip later)
        lines.push_back(line);
    }
    if (lines.empty()) return;

    // find header line (contains Name and Id) and separator (----)
    int headerIdx = -1;
    int sepIdx = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (lines[i].find("----") != std::string::npos) {
            sepIdx = i;
            break;
        }
    }
    if (sepIdx <= 0) return;
    headerIdx = sepIdx - 1;
    std::string header = lines[headerIdx];

    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };

    // determine column start positions from header
    std::vector<int> colStarts;
    std::vector<std::string> colNames = {"Name","Id","Version","Available","Source"};
    for (auto &cn : colNames) {
        size_t p = header.find(cn);
        if (p != std::string::npos) colStarts.push_back((int)p);
    }
    if (colStarts.size() < 2) {
        // fallback: whitespace token parsing
        for (int i = sepIdx + 1, lastAdded = -1; i < (int)lines.size(); ++i) {
            const std::string &ln = lines[i];
            if (ln.find("upgrades available") != std::string::npos) break;
            if (trim(ln).empty()) continue;
            std::istringstream ls(ln);
            std::vector<std::string> toks;
            std::string tok;
            while (ls >> tok) toks.push_back(tok);
            if (toks.size() < 4) continue;
                // Look for pattern: <name...> <id> <installed-version> <available-version>
                std::regex verRe2(R"(^[0-9]+(\.[0-9]+)*$)");
                size_t n = toks.size();
                // require at least two trailing version-like tokens
                if (n >= 3 && std::regex_match(toks[n-1], verRe2) && std::regex_match(toks[n-2], verRe2)) {
                    std::string available = toks[n-1];
                    std::string installed = toks[n-2];
                    std::string id = toks[n-3];
                    std::string name;
                    for (size_t j = 0; j + 3 < toks.size(); ++j) {
                        if (j) name += " ";
                        name += toks[j];
                    }
                    if (name.empty()) name = id;
                    if (CompareVersions(installed, available) < 0) g_packages.emplace_back(id, name);
                } else {
                    // fallback: if we can't detect versions, skip to avoid false positives
                    continue;
                }
            lastAdded = (int)g_packages.size()-1;
        }
        return;
    }

    // add end position as line length sentinel per line when slicing
    int lastAdded = -1;
    for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
        const std::string &ln = lines[i];
        if (ln.find("upgrades available") != std::string::npos) break;
        if (trim(ln).empty()) continue;
        // if the line is shorter than second column start, treat as continuation
        if ((int)ln.size() <= colStarts[1]) {
            if (lastAdded >= 0) {
                std::string cont = trim(ln);
                if (!cont.empty()) {
                    g_packages[lastAdded].second += " ";
                    g_packages[lastAdded].second += cont;
                }
            }
            continue;
        }
        auto substrSafe = [&](const std::string &s, int a, int b)->std::string{
            int len = (int)s.size();
            if (a >= len) return std::string();
            int end = std::min(len, b);
            return s.substr(a, end - a);
        };

        // compute per-column substrings using next column start or line end
        int ncols = (int)colStarts.size();
        std::vector<std::string> fields(ncols);
        for (int c = 0; c < ncols; ++c) {
            int a = colStarts[c];
            int b = (c+1 < ncols) ? colStarts[c+1] : (int)ln.size();
            fields[c] = trim(substrSafe(ln, a, b));
        }
        std::string name = fields[0];
        std::string id = (ncols > 1) ? fields[1] : std::string();
        if (id.empty()) {
            // treat as continuation if id missing
            if (lastAdded >= 0) {
                std::string cont = trim(ln);
                if (!cont.empty()) {
                    g_packages[lastAdded].second += " ";
                    g_packages[lastAdded].second += cont;
                }
            }
            continue;
        }
        if (name.empty()) name = id;
        g_packages.emplace_back(id, name);
        lastAdded = (int)g_packages.size()-1;
    }

    // Additional heuristic pass: look for tokens like <Id-with-dot> <version>
    // to catch lines where wrapping confused column slicing.
    std::set<std::string> seenIds;
    for (auto &p : g_packages) seenIds.insert(p.first);
    std::regex verRe(R"(^[0-9]+(\.[0-9]+)*$)");
    for (const auto &ln : lines) {
        std::istringstream ls(ln);
        std::vector<std::string> toks;
        std::string tok;
        while (ls >> tok) toks.push_back(tok);
        // look for pattern: ... <id> <installed> <available>
        for (size_t j = 0; j + 2 < toks.size(); ++j) {
            if (std::regex_match(toks[j+1], verRe) && std::regex_match(toks[j+2], verRe)) {
                std::string id = toks[j];
                if (seenIds.count(id)) break;
                std::string installed = toks[j+1];
                std::string available = toks[j+2];
                if (CompareVersions(installed, available) < 0) {
                    std::string name;
                    for (size_t k = 0; k < j; ++k) {
                        if (k) name += " ";
                        name += toks[k];
                    }
                    if (name.empty()) name = id;
                    g_packages.emplace_back(id, name);
                    seenIds.insert(id);
                }
                break;
            }
        }
    }
    }

static void PopulateListView(HWND hList) {
    ListView_DeleteAllItems(hList);
    LVITEMW lvi{};
    lvi.mask = LVIF_TEXT | LVIF_PARAM;
    for (int i = 0; i < (int)g_packages.size(); ++i) {
        std::string name = g_packages[i].second;
        std::string id = g_packages[i].first;
        std::wstring wname = Utf8ToWide(name);
        std::wstring wid = Utf8ToWide(id);
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)wname.c_str();
        lvi.lParam = i;
        SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        LVITEMW lvi2{};
        lvi2.mask = LVIF_TEXT;
        lvi2.iItem = i;
        lvi2.iSubItem = 1;
        lvi2.pszText = (LPWSTR)wid.c_str();
        SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&lvi2);
    }
}

// Helper used by custom draw / notifications: check if item index corresponds to NotApplicable id
static bool IsItemNotApplicable(int index) {
    if (index < 0 || index >= (int)g_packages.size()) return false;
    std::string id = g_packages[index].first;
    std::lock_guard<std::mutex> lk(g_packages_mutex);
    return g_not_applicable_ids.find(id) != g_not_applicable_ids.end();
}

// Parse the standard `winget upgrade` table which has columns: Name | Id | Version | Available
// Add entries where Available > Version.
static void ParseWingetUpgradeTableForUpdates(const std::string &text, std::set<std::pair<std::string,std::string>> &outSet) {
    std::istringstream iss(text);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(iss, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty()) return;

    int headerIdx = -1, sepIdx = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (lines[i].find("----") != std::string::npos) { sepIdx = i; break; }
    }
    if (sepIdx <= 0) return;
    headerIdx = sepIdx - 1;
    std::string header = lines[headerIdx];

    auto trim = [](std::string s){ while(!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin()); while(!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); return s; };
    std::vector<std::string> colNames = {"Name","Id","Version","Available"};
    std::vector<int> colStarts;
    for (auto &cn : colNames) {
        size_t p = header.find(cn);
        if (p != std::string::npos) colStarts.push_back((int)p);
    }
    if (colStarts.size() < 3) return;

    for (int i = sepIdx + 1; i < (int)lines.size(); ++i) {
        const std::string &ln = lines[i];
        if (trim(ln).empty()) continue;
        // stop on summary line
        if (ln.find("upgrades available") != std::string::npos) break;
        int ncols = (int)colStarts.size();
        auto substrSafe = [&](const std::string &s, int a, int b)->std::string{
            int len = (int)s.size();
            if (a >= len) return std::string();
            int end = std::min(len, b);
            return s.substr(a, end - a);
        };
        std::vector<std::string> fields(ncols);
        for (int c = 0; c < ncols; ++c) {
            int a = colStarts[c];
            int b = (c+1 < ncols) ? colStarts[c+1] : (int)ln.size();
            fields[c] = trim(substrSafe(ln, a, b));
        }
        std::string name = fields[0];
        std::string id = (ncols > 1) ? fields[1] : std::string();
        std::string installed = (ncols > 2) ? fields[2] : std::string();
        std::string available = (ncols > 3) ? fields[3] : std::string();
        if (id.empty()) continue;
        if (CompareVersions(installed, available) < 0) outSet.emplace(id, name.empty()?id:name);
    }
}

// Dump parsed packages and current ListView items to a temp file for debugging
static std::wstring DumpPackagesAndListViewToTemp(HWND hList) {
    wchar_t curDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, curDir);
    unsigned long long uniq = GetTickCount64();
    wchar_t outfn[MAX_PATH];
    swprintf(outfn, _countof(outfn), L"%s\\wup_dump_%llu.txt", curDir, uniq);
    std::string nfn = WideToUtf8(outfn);
    std::ofstream ofs(nfn, std::ios::binary);
    if (!ofs) return std::wstring();
    ofs << "Parsed packages:\r\n";
    for (auto &p : g_packages) {
        ofs << p.first << "\t" << p.second << "\r\n";
    }
    ofs << "\r\nListView items:\r\n";
    int cnt = ListView_GetItemCount(hList);
    for (int i = 0; i < cnt; ++i) {
        wchar_t buf[1024] = {0};
        LVITEMW lvi{};
        lvi.iItem = i; lvi.iSubItem = 0; lvi.mask = LVIF_TEXT; lvi.pszText = buf; lvi.cchTextMax = _countof(buf);
        SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi);
        std::wstring wtext(buf);
        std::string stext = WideToUtf8(wtext);
        ofs << stext << "\r\n";
    }
    ofs.close();
    return std::wstring(outfn);
}

// Write arbitrary UTF-8 text to a temp file and return its path
static std::wstring WriteDebugTextToTemp(const std::string &txt) {
    wchar_t curDir[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, curDir);
    unsigned long long uniq = GetTickCount64();
    wchar_t outfn[MAX_PATH];
    swprintf(outfn, _countof(outfn), L"%s\\wupdbg_%llu.txt", curDir, uniq);
    std::string nfn = WideToUtf8(outfn);
    std::ofstream ofs(nfn, std::ios::binary);
    if (!ofs) return std::wstring();
    ofs << txt;
    ofs.close();
    return std::wstring(outfn);
}

static void CheckAllItems(HWND hList, bool check) {
    int count = ListView_GetItemCount(hList);
    for (int i = 0; i < count; ++i) ListView_SetCheckState(hList, i, check);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hRadioShow, hRadioAll, hBtnRefresh, hList, hCheckAll, hBtnUpgrade;
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icce);

        // Title (H2-like centered)
        HDC hdcTitle = GetDC(hwnd);
        int lfTitleHeight = -MulDiv(14, GetDeviceCaps(hdcTitle, LOGPIXELSY), 72);
        ReleaseDC(hwnd, hdcTitle);
        g_hTitleFont = CreateFontW(lfTitleHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hTitle = CreateWindowExW(0, L"Static", L"WinUpdate", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 600, 28, hwnd, NULL, NULL, NULL);
        if (g_hTitle && g_hTitleFont) SendMessageW(g_hTitle, WM_SETFONT, (WPARAM)g_hTitleFont, TRUE);

        // language selection combobox (top-right)
        HWND hComboLang = CreateWindowExW(0, WC_COMBOBOXW, NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 470, 10, 150, 200, hwnd, (HMENU)IDC_COMBO_LANG, NULL, NULL);
        if (hComboLang) {
            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)L"English (en)");
            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)L"Norsk (no)");
            // select based on g_locale (prefix)
            int sel = 0;
            if (g_locale.rfind("no",0) == 0) sel = 1;
            SendMessageW(hComboLang, CB_SETCURSEL, sel, 0);
        }

        // Last-updated label (small bold ~9pt) placed under the title and centered
        HDC hdc = GetDC(hwnd);
        int lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(hwnd, hdc);
        g_hLastUpdatedFont = CreateFontW(lfHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g_hLastUpdated = CreateWindowExW(0, L"Static", L"List last updated: N/A", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 40, 600, 16, hwnd, NULL, NULL, NULL);
        if (g_hLastUpdated && g_hLastUpdatedFont) SendMessageW(g_hLastUpdated, WM_SETFONT, (WPARAM)g_hLastUpdatedFont, TRUE);

        hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, NULL, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 10, 60, 600, 284, hwnd, (HMENU)IDC_LISTVIEW, NULL, NULL);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 360;
        static std::wstring pkgCol = t("package_col");
        static std::wstring idCol = t("id_col");
        static wchar_t *colText = (wchar_t*)pkgCol.c_str();
        col.pszText = colText;
        ListView_InsertColumn(hList, 0, &col);
        LVCOLUMNW col2{};
        col2.mask = LVCF_TEXT | LVCF_WIDTH;
        col2.cx = 200;
        col2.pszText = (wchar_t*)idCol.c_str();
        ListView_InsertColumn(hList, 1, &col2);

        hCheckAll = CreateWindowExW(0, L"Button", t("select_all").c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 10, 350, 120, 24, hwnd, (HMENU)IDC_CHECK_SELECTALL, NULL, NULL);
        // place Upgrade button 5px to the right of Select all
        hBtnUpgrade = CreateWindowExW(0, L"Button", t("upgrade_now").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 135, 350, 140, 28, hwnd, (HMENU)IDC_BTN_UPGRADE, NULL, NULL);
        // position Refresh where the Upgrade button used to be (bottom-right)
        hBtnRefresh = CreateWindowExW(0, L"Button", t("refresh").c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 470, 350, 140, 28, hwnd, (HMENU)IDC_BTN_REFRESH, NULL, NULL);
        // record main window handle, initial timestamp and auto-refresh once UI is created (start async refresh)
        g_hMainWindow = hwnd;
        UpdateLastUpdatedLabel(hwnd);
        ShowLoading(hwnd);
        PostMessageW(hwnd, WM_REFRESH_ASYNC, 0, 0);
        break;
    }
    case WM_REFRESH_ASYNC: {
        // start background thread to perform winget query + parsing
        // disable Refresh button while running
        if (hBtnRefresh) EnableWindow(hBtnRefresh, FALSE);
        if (hBtnUpgrade) EnableWindow(hBtnUpgrade, FALSE);
        ShowLoading(hwnd);
        std::thread([hwnd]() {
            std::vector<std::pair<std::string,std::string>> results;

            // Fast-path: prefer a cached `winget list` and run `winget upgrade` with a short timeout.
            std::string listOut;
            try {
                namespace fs = std::filesystem;
                fs::path listPath = fs::current_path() / "wup_winget_list_fallback.txt";
                if (fs::exists(listPath)) {
                    try {
                        auto ftime = fs::last_write_time(listPath);
                        using file_clock = decltype(ftime)::clock;
                        auto file_now = file_clock::now();
                        auto sys_now = std::chrono::system_clock::now();
                        auto sctp = sys_now + (ftime - file_now);
                        auto age = std::chrono::system_clock::now() - sctp;
                        if (age < std::chrono::seconds(60)) {
                            listOut = ReadFileUtf8(listPath.wstring());
                        }
                    } catch(...) {}
                }
            } catch(...) {}

            if (listOut.empty()) {
                auto rlist = RunProcessCaptureExitCode(L"cmd /C winget list", 1000);
                listOut = rlist.second;
                if (!listOut.empty()) {
                    try { std::ofstream ofs("wup_winget_list_fallback.txt", std::ios::binary); if (ofs) ofs << listOut; } catch(...) {}
                }
            }

            // Run winget upgrade with a 2s timeout (fast). If it returns quickly, parse it; if it times out/empty, treat list candidates as NotApplicable.
            auto rup = RunProcessCaptureExitCode(L"cmd /C winget upgrade --accept-source-agreements --accept-package-agreements", 2000);
            std::string out = rup.second;
            bool timedOut = (rup.first == -2 || out.empty());
            // If winget produced no output or timed out, try any recent captured raw output files
            if (out.empty()) {
                std::string recent = ReadMostRecentRawWinget();
                if (!recent.empty()) {
                    out = recent;
                    timedOut = false; // treat as usable output
                }
            }

            if (!out.empty()) {
                bool sawNotApplicable = (out.find("A newer package version is available in a configured source, but it does not apply to your system or requirements") != std::string::npos) || (out.find("No applicable upgrade found") != std::string::npos);
                std::set<std::pair<std::string,std::string>> found;
                if (!listOut.empty()) FindUpdatesUsingKnownList(listOut, out, found);
                if (found.empty()) ParseUpgradeFast(out, found);

                if (!found.empty()) {
                    for (auto &p : found) results.emplace_back(p.first, p.second);
                } else if (sawNotApplicable && !listOut.empty()) {
                    // mark candidates as NotApplicable
                    std::set<std::string> localNA;
                    std::istringstream lss(listOut);
                    std::string ln;
                    std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                    std::smatch m;
                    while (std::getline(lss, ln)) {
                        if (std::regex_search(ln, m, anyRe)) {
                            std::string id = m[2].str();
                            std::string installed = m[3].str();
                            std::string available = m[4].str();
                            try { if (CompareVersions(installed, available) < 0) localNA.insert(id); } catch(...) {}
                        }
                    }
                    if (!localNA.empty()) {
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        for (auto &s : localNA) g_not_applicable_ids.insert(s);
                    }
                }
            }

            if (out.empty() || timedOut) {
                // As a fast fallback: use the cached list to identify candidates and mark them NotApplicable (do not perform slow probes).
                if (!listOut.empty()) {
                    std::istringstream lss(listOut);
                    std::string ln;
                    std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                    std::smatch m;
                    std::set<std::string> localNA;
                    while (std::getline(lss, ln)) {
                        if (std::regex_search(ln, m, anyRe)) {
                            std::string id = m[2].str();
                            std::string installed = m[3].str();
                            std::string available = m[4].str();
                            try { if (CompareVersions(installed, available) < 0) localNA.insert(id); } catch(...) {}
                        }
                    }
                    if (!localNA.empty()) {
                        std::lock_guard<std::mutex> lk(g_packages_mutex);
                        g_not_applicable_ids = localNA;
                    }
                }
            }

            auto *pv = new std::vector<std::pair<std::string,std::string>>(std::move(results));
            PostMessageA(hwnd, WM_REFRESH_DONE, 0, (LPARAM)pv);
        }).detach();
        break;
    }
    case WM_REFRESH_DONE: {
        std::vector<std::pair<std::string,std::string>> *pv = (std::vector<std::pair<std::string,std::string>>*)lParam;
        if (pv) {
            // update global packages and UI
            {
                std::lock_guard<std::mutex> lk(g_packages_mutex);
                g_packages = *pv;
            }
            delete pv;
            if (hList) PopulateListView(hList);
            // re-enable buttons
            if (hBtnRefresh) EnableWindow(hBtnRefresh, TRUE);
            if (hBtnUpgrade) EnableWindow(hBtnUpgrade, TRUE);
        }
        HideLoading();
        // If the refresh produced no parsed packages, show an 'up-to-date' dialog
        {
            std::lock_guard<std::mutex> lk(g_packages_mutex);
            if (g_packages.empty()) {
                std::wstring msg = t("your_system_updated");
                MessageBoxW(hwnd, msg.c_str(), L"WinUpdate", MB_OK | MB_ICONINFORMATION);
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndCtl = (HWND)lParam;
        // make static controls blend with window background (transparent) and
        // set last-updated text to dark green
        if (hwndCtl == g_hLastUpdated) {
            SetTextColor(hdcStatic, RGB(0,100,0));
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        if (hwndCtl == g_hTitle) {
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == LOADING_TIMER_ID) {
            // loading timer is handled by the popup window; ignore here
            return 0;
        }
        break;
    }
    case WM_NOTIFY: {
        LPNMHDR pnm = (LPNMHDR)lParam;
        if (!pnm) break;
        if (pnm->idFrom == IDC_LISTVIEW) {
            if (pnm->code == LVN_ITEMCHANGING) {
                LPNMLISTVIEW p = (LPNMLISTVIEW)lParam;
                if (p && (p->uChanged & LVIF_STATE)) {
                    // detect checkbox state change via state image mask
                    if (((p->uNewState ^ p->uOldState) & LVIS_STATEIMAGEMASK) != 0) {
                        int idx = p->iItem;
                        if (IsItemNotApplicable(idx)) {
                            // cancel the change to prevent checking NotApplicable items
                            return TRUE;
                        }
                    }
                }
            } else if (pnm->code == NM_CUSTOMDRAW) {
                LPNMTVCUSTOMDRAW cd = (LPNMTVCUSTOMDRAW)lParam;
                // Use custom draw for listview items to gray out NotApplicable text
                LPNMLVCUSTOMDRAW lvc = (LPNMLVCUSTOMDRAW)lParam;
                switch (lvc->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT: {
                    int idx = (int)lvc->nmcd.dwItemSpec;
                    if (IsItemNotApplicable(idx)) {
                        lvc->clrText = RGB(160,160,160);
                    }
                    return CDRF_DODEFAULT;
                }
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_COMBO_LANG && HIWORD(wParam) == CBN_SELCHANGE) {
            HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_LANG);
                if (hCombo) {
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                std::string newloc = (sel == 1) ? "no" : "en";
                g_locale = newloc;
                LoadLocaleFromFile(g_locale);
                SaveLocaleSetting(g_locale);
                // update UI texts
                UpdateLastUpdatedLabel(hwnd);
                SetWindowTextW(GetDlgItem(hwnd, IDC_CHECK_SELECTALL), t("select_all").c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_UPGRADE), t("upgrade_now").c_str());
                SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_REFRESH), t("refresh").c_str());
                // update listview column headers
                HWND hListLocal = GetDlgItem(hwnd, IDC_LISTVIEW);
                if (hListLocal) {
                    LVCOLUMNW col{};
                    col.mask = LVCF_TEXT;
                    std::wstring pkg = t("package_col");
                    std::wstring idc = t("id_col");
                    col.pszText = (LPWSTR)pkg.c_str();
                    SendMessageW(hListLocal, LVM_SETCOLUMNW, 0, (LPARAM)&col);
                    col.pszText = (LPWSTR)idc.c_str();
                    SendMessageW(hListLocal, LVM_SETCOLUMNW, 1, (LPARAM)&col);
                }
                // update window title but do not translate app name
                std::wstring winTitle = std::wstring(L"WinUpdate - ") + t("app_window_suffix");
                SetWindowTextW(hwnd, winTitle.c_str());
            }
            break;
        }
        if (id == IDC_BTN_REFRESH) {
            // update timestamp and start async refresh
            UpdateLastUpdatedLabel(hwnd);
            PostMessageW(hwnd, WM_REFRESH_ASYNC, 0, 0);
            break;
        } else if (id == IDC_CHECK_SELECTALL) {
            // Toggle check state for all list items when Select all checkbox is toggled
            HWND hChk = GetDlgItem(hwnd, IDC_CHECK_SELECTALL);
            BOOL ch = (SendMessageW(hChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
            CheckAllItems(hList, ch);
        } else if (id == IDC_BTN_UPGRADE) {
            // Use the Select-all checkbox to determine "upgrade all"
            BOOL allChecked = (SendMessageW(GetDlgItem(hwnd, IDC_CHECK_SELECTALL), BM_GETCHECK, 0, 0) == BST_CHECKED);
            // create temp batch file
            wchar_t tmpPath[MAX_PATH];
            wchar_t tmpFile[MAX_PATH];
            GetTempPathW(MAX_PATH, tmpPath);
            GetTempFileNameW(tmpPath, L"wup", 0, tmpFile);
            std::wofstream ofs(tmpFile);
            ofs << L"@echo off\r\n";
            if (allChecked) {
                ofs << L"winget upgrade --all --accept-source-agreements --accept-package-agreements\r\n";
            } else {
                // iterate checked items
                int count = ListView_GetItemCount(hList);
                for (int i = 0; i < count; ++i) {
                    if (ListView_GetCheckState(hList, i)) {
                        LVITEMW lvi{};
                        wchar_t buf[512];
                        lvi.iItem = i; lvi.iSubItem = 0; lvi.mask = LVIF_TEXT | LVIF_PARAM; lvi.pszText = buf; lvi.cchTextMax = _countof(buf);
                        SendMessageW(hList, LVM_GETITEMW, 0, (LPARAM)&lvi);
                        int idx = (int)lvi.lParam;
                        if (idx >= 0 && idx < (int)g_packages.size()) {
                            std::wstring idw(g_packages[idx].first.begin(), g_packages[idx].first.end());
                            ofs << L"winget upgrade --id \"" << idw << L"\" --accept-source-agreements --accept-package-agreements\r\n";
                        }
                    }
                }
            }
            ofs.close();

            // Run elevated (single UAC prompt) using ShellExecuteEx with verb runas
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.hwnd = hwnd;
            sei.lpVerb = L"runas";
            sei.lpFile = L"cmd.exe";
            std::wstring params = L"/C \"" + std::wstring(tmpFile) + L"\"";
            sei.lpParameters = params.c_str();
            sei.nShow = SW_SHOWNORMAL;
            if (!ShellExecuteExW(&sei)) {
                MessageBoxW(hwnd, L"Failed to launch elevated process.", L"Error", MB_ICONERROR);
            } else {
                // optionally wait or just return; don't block UI long
                // WaitForSingleObject(sei.hProcess, INFINITE);
                CloseHandle(sei.hProcess);
            }
            // Note: temp file left on disk; user can remove later if needed
        }
        break;
    }
    case WM_DESTROY:
        if (g_hLastUpdatedFont) {
            DeleteObject(g_hLastUpdatedFont);
            g_hLastUpdatedFont = NULL;
        }
        if (g_hTitleFont) {
            DeleteObject(g_hTitleFont);
            g_hTitleFont = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSW wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassW(&wc)) return 0;

    // Initialize translations: prefer saved language, then environment/OS locale
    InitDefaultTranslations();
    // First check settings file for saved language
    std::string saved = LoadLocaleSetting();
    if (!saved.empty()) {
        g_locale = saved;
    } else {
        // try to read environment LANG or LC_ALL
        char *env = nullptr;
        size_t envsz = 0;
        std::string sysloc;
        try {
            if (_dupenv_s(&env, &envsz, "LANG") == 0 && env) sysloc = std::string(env);
            else if (_dupenv_s(&env, &envsz, "LC_ALL") == 0 && env) sysloc = std::string(env);
        } catch(...) { }
        if (env) { free(env); env = nullptr; }
        if (!sysloc.empty()) {
            size_t p = sysloc.find_first_of("._");
            std::string prefix = (p==std::string::npos) ? sysloc : sysloc.substr(0,p);
            g_locale = prefix;
        }
        if (g_locale.empty()) {
            WCHAR buf[32] = {0};
            if (GetUserDefaultLocaleName(buf, (int)std::size(buf))) {
                std::wstring wln(buf);
                if (wln.size() >= 2) g_locale = std::string(wln.begin(), wln.begin()+2);
            }
        }
        if (g_locale.empty()) g_locale = "en";
    }
    // attempt to load translations for the locale
    LoadLocaleFromFile(g_locale);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"WinUpdate - winget GUI updater", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 430, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    // load and set application icons (embedded in resources)
    HICON hIconBig = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR);
    HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    if (hIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    ShowWindow(hwnd, nCmdShow);

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LPWSTR cmdLineW = GetCommandLineW();
    // If invoked with --debug-parse, run winget (text) and print parsed entries to console then exit
    std::wstring cmdLine(cmdLineW ? cmdLineW : L"");
    if (cmdLine.find(L"--debug-parse") != std::wstring::npos) {
        // create a console to print results
        AllocConsole();
        FILE *f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        // Read the run log (it contains prior winget captures which are more stable)
        std::string logtxt = ReadFileUtf8(L"wup_run_log.txt");
        if (logtxt.empty()) {
            // fallback to running winget directly
            std::wstring cmd = L"cmd /C winget upgrade --accept-source-agreements --accept-package-agreements";
            auto res = RunProcessCaptureExitCode(cmd, 15000);
            logtxt = res.second;
        }
        if (logtxt.empty()) {
            printf("No winget output available (run log empty).\n");
        } else {
            // Try fast parse of fresh winget upgrade output first
            std::set<std::pair<std::string,std::string>> found;
            std::wstring cmd = L"cmd /C winget upgrade --accept-source-agreements --accept-package-agreements";
            auto res = RunProcessCaptureExitCode(cmd, 15000);
            std::string listOut;
            if (!res.second.empty()) {
                // prefer list-based mapping first
                auto resList = RunProcessCaptureExitCode(L"cmd /C winget list", 8000);
                std::string listOut = resList.second;
                if (!listOut.empty() && !res.second.empty()) {
                    FindUpdatesUsingKnownList(listOut, res.second, found);
                }
                // If upgrade output indicates the 'not applicable' banner, mark list-based candidates as NotApplicable
                bool sawNotApplicable = (res.second.find("A newer package version is available in a configured source, but it does not apply to your system or requirements") != std::string::npos);
                if (sawNotApplicable && !listOut.empty()) {
                    // If winget reports 'not applicable', avoid trusting the whitespace parser (it can produce false positives).
                    // Instead, collect NotApplicable ids from the list output and report them.
                    std::istringstream lss2(listOut);
                    std::string line3;
                    std::set<std::string> localNA2;
                    std::regex anyRe2("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                    std::smatch m3;
                    while (std::getline(lss2, line3)) {
                        if (std::regex_search(line3, m3, anyRe2)) {
                            std::string id = m3[2].str();
                            std::string installed = m3[3].str();
                            std::string available = m3[4].str();
                            try { if (CompareVersions(installed, available) < 0) localNA2.insert(id); } catch(...) {}
                        }
                    }
                    if (!localNA2.empty()) {
                        printf("No applicable updates (all candidates are NotApplicable)\n");
                        printf("NotApplicable IDs:\n");
                        for (auto &s : localNA2) printf("%s\n", s.c_str());
                        return 0;
                    }
                }
                if (found.empty()) {
                    ParseUpgradeFast(res.second, found);
                }
                if (found.empty() && sawNotApplicable && !listOut.empty()) {
                    // collect ids from list where available > installed and report as NotApplicable
                    std::istringstream lss(listOut);
                    std::string line2;
                    std::set<std::string> localNA;
                    std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                    std::smatch m2;
                    while (std::getline(lss, line2)) {
                        if (std::regex_search(line2, m2, anyRe)) {
                            std::string id = m2[2].str();
                            std::string installed = m2[3].str();
                            std::string available = m2[4].str();
                            try { if (CompareVersions(installed, available) < 0) localNA.insert(id); } catch(...) {}
                        }
                    }
                    if (!localNA.empty()) {
                        printf("No applicable updates (all candidates are NotApplicable)\n");
                        printf("NotApplicable IDs:\n");
                        for (auto &s : localNA) printf("%s\n", s.c_str());
                        return 0;
                    }
                }
            }
            // If we have a list output, probe candidate ids for NotApplicable status
            std::set<std::string> candidateIds;
            if (!listOut.empty()) {
                std::istringstream lss(listOut);
                std::string lline;
                std::regex anyRe("([\\S ]+?)\\s+([^\\s]+)\\s+(\\S+)\\s+(\\S+)");
                std::smatch mm;
                while (std::getline(lss, lline)) {
                    if (std::regex_search(lline, mm, anyRe)) {
                        std::string id = mm[2].str();
                        std::string installed = mm[3].str();
                        std::string available = mm[4].str();
                        try { if (CompareVersions(installed, available) < 0) candidateIds.insert(id); } catch(...) {}
                    }
                }
            }

            std::set<std::string> localNA;
            // probe candidate ids that were not found by the generic parsing
            for (auto &id : candidateIds) {
                bool alreadyFound = false;
                for (auto &p : found) if (p.first == id) { alreadyFound = true; break; }
                if (alreadyFound) continue;
                // run per-id upgrade probe with short timeout
                std::wstring cmd = L"cmd /C winget upgrade --id \"" + std::wstring(id.begin(), id.end()) + L"\" --accept-source-agreements --accept-package-agreements";
                auto r = RunProcessCaptureExitCode(cmd, 2500);
                std::string out = r.second;
                if (out.find("does not apply to your system or requirements") != std::string::npos || out.find("No applicable upgrade found") != std::string::npos) {
                    localNA.insert(id);
                } else {
                    // if probe returns an applicable upgrade, try to extract id/name
                    std::set<std::pair<std::string,std::string>> f2;
                    ExtractUpdatesFromText(out, f2);
                    for (auto &pp : f2) if (pp.first == id) found.insert(pp);
                }
            }

            if (!localNA.empty()) {
                printf("NotApplicable IDs:\n");
                for (auto &s : localNA) printf("%s\n", s.c_str());
            }

            if (found.empty()) {
                printf("No updates found in run log.\n");
            } else {
                printf("Parsed packages (updates available):\n");
                for (auto &p : found) printf("Id: %s\tName: %s\n", p.first.c_str(), p.second.c_str());
            }
        }
        // exit immediately without pausing
        return 0;
    }
    return wWinMain(hInstance, hPrevInstance, cmdLineW, nCmdShow);
}
