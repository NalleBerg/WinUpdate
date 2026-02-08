#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <regex>
#include "resource.h"
#include "search.h"
#include "installed_apps.h"
#include "app_details.h"
#include "about.h"
#include "quit_handler.h"

// Control IDs
#define ID_SEARCH_BTN 1001
#define ID_END_SEARCH_BTN 1006
#define ID_INSTALLED_BTN 1007
#define ID_REFRESH_INSTALLED_BTN 1008
#define ID_ABOUT_BTN 1009
#define ID_QUIT_BTN 1010
#define ID_TAG_TREE 1002
#define ID_APP_LIST 1004
#define ID_LANG_COMBO 1005

// Window class name
const wchar_t CLASS_NAME[] = L"WinProgramManagerClass";

// Global variables
HWND g_hSearchBtn = NULL;
HWND g_hEndSearchBtn = NULL;
HWND g_hInstalledBtn = NULL;
HWND g_hRefreshInstalledBtn = NULL;
HWND g_hRefreshTooltip = NULL;
HWND g_hAboutBtn = NULL;
HWND g_hQuitBtn = NULL;
HWND g_hTagTree = NULL;
HWND g_hTagCountLabel = NULL;
HWND g_hAppList = NULL;
HWND g_hAppCountLabel = NULL;
HWND g_hCategoryLabel = NULL;  // Category name label
HWND g_hLangCombo = NULL;
HWND g_hSplitter = NULL;
HFONT g_hFont = NULL;
HFONT g_hBoldFont = NULL;  // Bold font for category name
HIMAGELIST g_hImageList = NULL;
HIMAGELIST g_hTreeImageList = NULL;
sqlite3* g_db = NULL;
Locale g_locale;
std::wstring g_currentLang = L"en_GB";

// Search state variables
bool g_searchActive = false;
std::wstring g_searchCategoryFilter;
std::wstring g_searchAppFilter;
bool g_searchCaseSensitive = false;
bool g_searchExactMatch = false;
bool g_searchUseRegex = false;
bool g_searchRefineResults = false;
std::vector<std::wstring> g_filteredCategories;
std::vector<std::wstring> g_allCategories;  // Store all categories for reset

// Loading dialog globals
static HWND g_loadingDlg = NULL;
static HWND g_spinnerCtrl = NULL;
static int g_spinnerFrame = 0;
static std::thread g_loadingThread;
static std::atomic<bool> g_loadingThreadRunning(false);

// Icon loading dialog globals
static HWND g_hIconLoadingDialog = nullptr;
static std::atomic<bool> g_iconLoadingRunning(false);

// Global for main window handle
static HWND g_mainWindow = NULL;

// Loading dialog window procedure
LRESULT CALLBACK LoadingDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    
    if (uMsg == WM_TIMER && wParam == 1) {
        const wchar_t* frames[] = { L"◐", L"◓", L"◑", L"◒" };
        g_spinnerFrame = (g_spinnerFrame + 1) % 4;
        if (g_spinnerCtrl && IsWindow(g_spinnerCtrl)) {
            SetWindowTextW(g_spinnerCtrl, frames[g_spinnerFrame]);
        }
        return 0;
    }
    else if (uMsg == WM_CTLCOLORSTATIC) {
        HDC hdcStatic = (HDC)wParam;
        HWND hStatic = (HWND)lParam;
        
        // Use opaque mode with white background
        SetBkMode(hdcStatic, OPAQUE);
        SetBkColor(hdcStatic, RGB(255, 255, 255)); // White background
        
        // If it's the spinner control, make it blue
        if (hStatic == g_spinnerCtrl) {
            SetTextColor(hdcStatic, RGB(0, 120, 215)); // Windows blue
        } else {
            SetTextColor(hdcStatic, RGB(0, 0, 0)); // Black for other text
        }
        
        return (LRESULT)hWhiteBrush;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Loading dialog thread function - runs completely independently
DWORD WINAPI LoadingDialogThread(LPVOID lpParam) {
    HINSTANCE hInstance = (HINSTANCE)lpParam;
    
    // Register window class
    WNDCLASSW loadWc = {};
    loadWc.lpfnWndProc = LoadingDialogProc;
    loadWc.hInstance = hInstance;
    loadWc.lpszClassName = L"LoadingDialogClass";
    loadWc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    loadWc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&loadWc);
    
    // Create dialog
    g_loadingDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"LoadingDialogClass",
        g_locale.title.c_str(),
        WS_POPUP | WS_VISIBLE | WS_CAPTION,
        0, 0, 500, 300,
        NULL, NULL, hInstance, NULL
    );
    
    // Center dialog
    RECT rc;
    GetWindowRect(g_loadingDlg, &rc);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
    SetWindowPos(g_loadingDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    
    // Bring to foreground initially but not topmost (allows switching away)
    SetForegroundWindow(g_loadingDlg);
    BringWindowToTop(g_loadingDlg);
    
    // Add logo
    HICON hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 128, 128, LR_DEFAULTCOLOR);
    if (hIcon) {
        HWND hLogo = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            186, 20, 128, 128, g_loadingDlg, NULL, hInstance, NULL);
        SendMessageW(hLogo, STM_SETICON, (WPARAM)hIcon, 0);
    }
    
    // Add label - create empty, set text after applying font (like processing_database pattern)
    HWND hLabel = CreateWindowExW(0, L"STATIC", L"", 
        WS_CHILD | WS_VISIBLE | SS_CENTER, 50, 160, 400, 35, g_loadingDlg, (HMENU)102, hInstance, NULL);
    
    // Create persistent font for label (static so it doesn't get destroyed) - larger and bold
    static HFONT hLabelFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(hLabel, WM_SETFONT, (WPARAM)hLabelFont, TRUE);
    
    // Set text AFTER font is applied (matches processing_database pattern that works)
    SetDlgItemTextW(g_loadingDlg, 102, g_locale.querying_winget.c_str());
    
    // Force update
    UpdateWindow(hLabel);
    
    // Add spinner
    g_spinnerCtrl = CreateWindowExW(0, L"STATIC", L"◐", WS_CHILD | WS_VISIBLE | SS_CENTER,
        200, 200, 100, 80, g_loadingDlg, NULL, hInstance, NULL);
    
    HFONT hFont = CreateFontW(60, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SendMessageW(g_spinnerCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Color will be set via WM_CTLCOLORSTATIC
    
    // Make spinner blue - we'll handle this in WM_CTLCOLORSTATIC
    
    UpdateWindow(g_loadingDlg);
    
    // Start timer
    SetTimer(g_loadingDlg, 1, 60, NULL);
    
    g_loadingThreadRunning = true;
    
    // Independent message loop - runs until dialog is destroyed
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!g_loadingThreadRunning) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return 0;
}

int g_splitterPos = 300;  // Initial splitter position
bool g_draggingSplitter = false;
std::wstring g_selectedTag = L"All";
std::map<int, std::vector<unsigned char>> g_iconCache;
std::vector<std::wstring*> g_tagTextBuffers;  // Persistent storage for TreeView text

// Structures
struct AppInfo {
    int id;
    std::wstring packageId;
    std::wstring name;
    std::wstring version;
    std::wstring publisher;
    std::wstring homepage;
    int iconIndex;
    std::vector<std::wstring> categories; // Categories this app belongs to
};

struct TagInfo {
    std::wstring name;
    int count;
};

// In-memory data cache for fast searching (declared after AppInfo)
std::vector<AppInfo> g_allApps;  // All apps loaded into memory (metadata only, not icons)
std::vector<std::wstring> g_allCategoryNames;  // All unique category names
std::map<std::wstring, std::vector<int>> g_categoryToAppIds;  // Map category name to app IDs

// Forward declarations
INT_PTR CALLBACK SearchDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK IconLoadingDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void ResizeControls(HWND hwnd);
bool OpenDatabase();
void CloseDatabase();
void LoadAllDataIntoMemory();  // Load all apps and categories into memory for fast search
void LoadAllIcons();  // Load all app icons into ImageList (call after ImageList is created)
void LoadInstalledPackageIds();  // Load installed package IDs from database
HBITMAP LoadIconFromBlob(const std::vector<unsigned char>& data, const std::wstring& type);
HICON LoadIconFromMemory(const unsigned char* data, int size);
void OnTagSelectionChanged();
void OnAppDoubleClick();
void OnLanguageChanged();
void ExecuteSearch();
void EndSearch();
bool MatchString(const std::wstring& text, const std::wstring& pattern);
bool LoadLocale(const std::wstring& lang);
std::wstring FormatNumber(int num);
std::wstring CapitalizeFirst(const std::wstring& str);
void SaveConfig();
void LoadConfig();

// WinMain - Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;
    (void)nCmdShow;

    // Load saved preferences (language)
    LoadConfig();

    // Load default locale - if fails, use fallback defaults
    if (!LoadLocale(g_currentLang)) {
        // Use English fallback defaults
        g_locale.lang_name = L"English (UK)";
        g_locale.thousands_sep = L",";
        g_locale.decimal_sep = L".";
        g_locale.categories = L"categories";
        g_locale.apps = L"apps";
        g_locale.search_tags = L"Search tags...";
        g_locale.search_apps = L"Search applications...";
        g_locale.all = L"All";
        g_locale.title = L"WinProgramManager";
        g_locale.processing_database = L"Processing database...";
        g_locale.wait_moment = L"Wait a moment....";
        g_locale.repopulating_table = L"Repopulating table...";
        g_locale.querying_winget = L"Checking installed apps with winget.\nThis can take some time...";
        g_locale.refresh_installed_btn = L"Refresh Installed Apps";
        g_locale.refresh_installed_tooltip = L"Discover apps installed outside this manager.\nThis may take a while.";
    }

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // Register window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));

    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, g_locale.window_registration_failed.c_str(), g_locale.error_title.c_str(), MB_ICONERROR | MB_OK);
        return 0;
    }

    // Start loading dialog in its own thread
    g_loadingThread = std::thread(LoadingDialogThread, hInstance);
    
    // Wait for loading dialog to be created
    while (!g_loadingThreadRunning) {
        Sleep(10);
    }
    Sleep(100); // Give it time to show

    // Create window with 80% screen height and centered with top margin
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 1000;  // Narrower window
    int windowHeight = (int)(screenHeight * 0.8);
    int windowX = (screenWidth - windowWidth) / 2;
    int windowY = (int)(screenHeight * 0.05);  // Start 5% from top to avoid taskbar at bottom
    
    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        g_locale.title.c_str(),
        WS_OVERLAPPEDWINDOW,
        windowX, windowY,
        windowWidth, windowHeight,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        MessageBoxW(NULL, g_locale.window_creation_failed.c_str(), g_locale.error_title.c_str(), MB_ICONERROR | MB_OK);
        return 0;
    }

    g_mainWindow = hwnd;
    // Don't show main window yet - will be shown by WM_USER after loading completes
    
    // Load accelerator table
    HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCELERATOR1));
    
    // Message loop with accelerator support
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAcceleratorW(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

// Search dialog procedure
INT_PTR CALLBACK SearchDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    
    switch (uMsg) {
        case WM_INITDIALOG: {
            // Center the dialog on parent window
            RECT rcParent, rcDlg;
            HWND hwndParent = GetParent(hwndDlg);
            GetWindowRect(hwndParent, &rcParent);
            GetWindowRect(hwndDlg, &rcDlg);
            
            int x = rcParent.left + (rcParent.right - rcParent.left - (rcDlg.right - rcDlg.left)) / 2;
            int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDlg.bottom - rcDlg.top)) / 2;
            SetWindowPos(hwndDlg, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
            
            // Set previous search values
            SetDlgItemTextW(hwndDlg, IDC_SEARCH_CATEGORY, g_searchCategoryFilter.c_str());
            SetDlgItemTextW(hwndDlg, IDC_SEARCH_APP, g_searchAppFilter.c_str());
            
            // Set radio button states
            CheckDlgButton(hwndDlg, g_searchCaseSensitive ? IDC_CASE_SENSITIVE : IDC_CASE_INSENSITIVE, BST_CHECKED);
            CheckDlgButton(hwndDlg, g_searchExactMatch ? IDC_EXACT_MATCH : IDC_CONTAINS, BST_CHECKED);
            CheckDlgButton(hwndDlg, IDC_USE_REGEX, g_searchUseRegex ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, g_searchRefineResults ? IDC_REFINE_RESULTS : IDC_SEARCH_ALL, BST_CHECKED);
            
            // Disable refine results if no active search
            EnableWindow(GetDlgItem(hwndDlg, IDC_REFINE_RESULTS), g_searchActive ? TRUE : FALSE);
            
            // If regex is checked, disable case/match options
            if (g_searchUseRegex) {
                EnableWindow(GetDlgItem(hwndDlg, IDC_CASE_INSENSITIVE), FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_CASE_SENSITIVE), FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_CONTAINS), FALSE);
                EnableWindow(GetDlgItem(hwndDlg, IDC_EXACT_MATCH), FALSE);
            }
            
            return TRUE;
        }
        
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDC_USE_REGEX) {
                // Enable/disable case and match options based on regex checkbox
                BOOL useRegex = IsDlgButtonChecked(hwndDlg, IDC_USE_REGEX) == BST_CHECKED;
                EnableWindow(GetDlgItem(hwndDlg, IDC_CASE_INSENSITIVE), !useRegex);
                EnableWindow(GetDlgItem(hwndDlg, IDC_CASE_SENSITIVE), !useRegex);
                EnableWindow(GetDlgItem(hwndDlg, IDC_CONTAINS), !useRegex);
                EnableWindow(GetDlgItem(hwndDlg, IDC_EXACT_MATCH), !useRegex);
                return TRUE;
            }
            
            if (LOWORD(wParam) == IDOK) {
                // Get search criteria from dialog
                wchar_t catBuf[512] = {0};
                wchar_t appBuf[512] = {0};
                GetDlgItemTextW(hwndDlg, IDC_SEARCH_CATEGORY, catBuf, 512);
                GetDlgItemTextW(hwndDlg, IDC_SEARCH_APP, appBuf, 512);
                
                g_searchCategoryFilter = catBuf;
                g_searchAppFilter = appBuf;
                
                g_searchCaseSensitive = IsDlgButtonChecked(hwndDlg, IDC_CASE_SENSITIVE) == BST_CHECKED;
                g_searchExactMatch = IsDlgButtonChecked(hwndDlg, IDC_EXACT_MATCH) == BST_CHECKED;
                g_searchUseRegex = IsDlgButtonChecked(hwndDlg, IDC_USE_REGEX) == BST_CHECKED;
                g_searchRefineResults = IsDlgButtonChecked(hwndDlg, IDC_REFINE_RESULTS) == BST_CHECKED;
                
                EndDialog(hwndDlg, IDOK);
                return TRUE;
            }
            
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        
        case WM_CLOSE: {
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
    }
    
    return FALSE;
}

// Helper function for string matching
bool MatchString(const std::wstring& text, const std::wstring& pattern) {
    if (pattern.empty()) return true;
    
    if (g_searchUseRegex) {
        try {
            std::wregex re(pattern, std::regex_constants::icase);
            return std::regex_search(text, re);
        } catch (...) {
            return false;  // Invalid regex
        }
    }
    
    std::wstring textCopy = text;
    std::wstring patternCopy = pattern;
    
    if (!g_searchCaseSensitive) {
        std::transform(textCopy.begin(), textCopy.end(), textCopy.begin(), ::towlower);
        std::transform(patternCopy.begin(), patternCopy.end(), patternCopy.begin(), ::towlower);
    }
    
    if (g_searchExactMatch) {
        return textCopy == patternCopy;
    } else {
        return textCopy.find(patternCopy) != std::wstring::npos;
    }
}

// Execute search based on current criteria
void ExecuteSearch() {
    if (g_allApps.empty()) return;  // No data loaded
    
    // Populate g_allCategories if empty (for potential future use)
    if (g_allCategories.empty()) {
        g_allCategories = g_allCategoryNames;
    }
    
    // Clear previous results
    g_filteredCategories.clear();
    ListView_DeleteAllItems(g_hTagTree);
    
    // Determine which categories to search
    std::vector<std::wstring> categoriesToSearch;
    
    if (!g_searchCategoryFilter.empty()) {
        // Filter categories based on search criteria (in-memory)
        for (const auto& category : g_allCategoryNames) {
            if (MatchString(category, g_searchCategoryFilter)) {
                categoriesToSearch.push_back(category);
            }
        }
    } else {
        // No category filter - use all categories
        categoriesToSearch = g_allCategoryNames;
    }
    
    // Now filter by app name if specified, and count matching apps per category
    int displayIndex = 0;
    for (const auto& category : categoriesToSearch) {
        int matchingAppCount = 0;
        
        // Get app IDs for this category
        auto it = g_categoryToAppIds.find(category);
        if (it != g_categoryToAppIds.end()) {
            for (int appId : it->second) {
                // Find the app in g_allApps
                for (const auto& app : g_allApps) {
                    if (app.id == appId) {
                        // Check if app matches app filter (if any)
                        if (g_searchAppFilter.empty() || MatchString(app.name, g_searchAppFilter)) {
                            matchingAppCount++;
                        }
                        break;
                    }
                }
            }
        }
        
        // Only add category if it has matching apps
        if (matchingAppCount > 0) {
            g_filteredCategories.push_back(category);
            
            // Add to ListView
            std::wstring* displayText = new std::wstring(L"   " + category);
            g_tagTextBuffers.push_back(displayText);
            
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
            lvi.iItem = displayIndex++;
            lvi.iSubItem = 0;
            lvi.pszText = (LPWSTR)displayText->c_str();
            lvi.lParam = (LPARAM)displayText;
            lvi.iImage = 0; // Closed folder
            ListView_InsertItem(g_hTagTree, &lvi);
        }
    }
    
    // Update category count
    int categoryCount = g_filteredCategories.size();
    SetWindowTextW(g_hTagCountLabel, L"                                        ");
    std::wstring countText = FormatNumber(categoryCount) + L" " + g_locale.categories;
    SetWindowTextW(g_hTagCountLabel, countText.c_str());
    
    // Select first category if any results
    if (categoryCount > 0) {
        ListView_SetItemState(g_hTagTree, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        OnTagSelectionChanged();
    } else {
        ListView_DeleteAllItems(g_hAppList);
        SetWindowTextW(g_hAppCountLabel, (L"0 " + g_locale.apps).c_str());
    }
    
    // Mark search as active
    g_searchActive = true;
    
    // Show End Search button
    ShowWindow(g_hEndSearchBtn, SW_SHOW);
    ResizeControls(g_mainWindow);
}

// Helper to keep dialog responsive during long operations
void ProcessDialogMessages() {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (!IsDialogMessageW(g_hIconLoadingDialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// End search and restore all data
void EndSearch() {
    // Create and show loading dialog - timer starts automatically
    g_hIconLoadingDialog = CreateDialog((HINSTANCE)GetWindowLongPtr(g_mainWindow, GWLP_HINSTANCE), 
                                        MAKEINTRESOURCE(IDD_LOADING_DIALOG), g_mainWindow, IconLoadingDialogProc);
    if (!g_hIconLoadingDialog) return;
    
    SetDlgItemTextW(g_hIconLoadingDialog, IDC_LOADING_TEXT, g_locale.repopulating_table.c_str());
    ShowWindow(g_hIconLoadingDialog, SW_SHOW);
    UpdateWindow(g_hIconLoadingDialog);
    
    // Process a few messages to let dialog initialize and start spinning
    MSG msg;
    for (int i = 0; i < 10; i++) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(16); // ~60fps
    }
    
    // Clear search state
    g_searchActive = false;
    g_searchCategoryFilter.clear();
    g_searchAppFilter.clear();
    g_searchCaseSensitive = false;
    g_searchExactMatch = false;
    g_searchUseRegex = false;
    g_searchRefineResults = false;
    g_filteredCategories.clear();
    
    // Do the work on main thread (these are UI operations)
    LoadTags();
    OnTagSelectionChanged();
    
    // Hide End Search button
    ShowWindow(g_hEndSearchBtn, SW_HIDE);
    ResizeControls(g_mainWindow);
    
    // Destroy dialog (spinner dies with it)
    if (g_hIconLoadingDialog) {
        DestroyWindow(g_hIconLoadingDialog);
        g_hIconLoadingDialog = nullptr;
    }
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Create font (19pt = 16pt * 1.20)
            g_hFont = CreateFontW(19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            
            // Create bold font for category name
            g_hBoldFont = CreateFontW(19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            // Post message to start loading
            PostMessageW(hwnd, WM_USER + 1, 0, 0);
            
            return 0;
        }
        
        case WM_USER + 1: {
            // Two-phase startup: 1) Discover installed apps via winget list, 2) Process database
            // Text already set to querying_winget when dialog was created
            
            std::thread([hwnd]() {
                // Phase 1: Open database first (needed for discovery)
                if (!OpenDatabase()) {
                    MessageBoxW(hwnd, g_locale.database_open_failed.c_str(), g_locale.error_title.c_str(), MB_ICONERROR | MB_OK);
                    PostQuitMessage(1);
                    return;
                }
                
                // Phase 2: Run winget list discovery (this is the slow part - can take 60+ seconds)
                DiscoverInstalledApps(g_db);
                
                // Phase 3: Update spinner text for final UI population
                if (g_loadingDlg && IsWindow(g_loadingDlg)) {
                    SetDlgItemTextW(g_loadingDlg, 102, g_locale.processing_database.c_str());
                }
                
                // All phases complete - signal main window to continue
                PostMessageW(hwnd, WM_USER, 0, 0);
            }).detach();
            
            return 0;
        }
        
        case WM_USER + 2: {
            // Async discovery completed (wParam: 1=success, 0=failure)
            bool success = (wParam != 0);
            
            // Destroy spinner
            if (g_hIconLoadingDialog) {
                DestroyWindow(g_hIconLoadingDialog);
                g_hIconLoadingDialog = nullptr;
            }
            
            if (success) {
                // Refresh the UI to show newly discovered apps
                LoadTags();
                OnTagSelectionChanged();
            } else {
                MessageBoxW(hwnd, g_locale.discovery_failed_msg.c_str(), g_locale.discovery_failed_title.c_str(), MB_ICONWARNING | MB_OK);
            }
            
            return 0;
        }
        
        case WM_USER: {
            // Database loaded - create controls and load data
            CreateControls(hwnd);
            
            // Force button redraw to show locale text
            if (g_hRefreshInstalledBtn) {
                InvalidateRect(g_hRefreshInstalledBtn, NULL, TRUE);
            }
            if (g_hAboutBtn) {
                InvalidateRect(g_hAboutBtn, NULL, TRUE);
            }
            
            // Load all icons into ImageList (must happen after ImageList is created)
            LoadAllIcons();
            
            // Load data into controls
            LoadTags();
            OnTagSelectionChanged();  // Load apps for initial selection
            
            // Signal loading thread to stop and destroy dialog BEFORE showing window
            g_loadingThreadRunning = false;
            if (g_loadingDlg && IsWindow(g_loadingDlg)) {
                PostMessageW(g_loadingDlg, WM_CLOSE, 0, 0);
            }
            
            // Wait for thread to finish
            if (g_loadingThread.joinable()) {
                g_loadingThread.join();
            }
            
            g_loadingDlg = NULL;
            g_spinnerCtrl = NULL;
            
            // Now show the main window with all data loaded
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
            
            // Force About button to redraw with locale text
            if (g_hAboutBtn && IsWindow(g_hAboutBtn)) {
                InvalidateRect(g_hAboutBtn, NULL, TRUE);
                UpdateWindow(g_hAboutBtn);
            }
            
            // Bring window to foreground
            SetForegroundWindow(hwnd);
            BringWindowToTop(hwnd);
            
            // Set focus to category list for blue selection
            SetFocus(g_hTagTree);
            
            return 0;
        }

        case WM_SIZE: {
            ResizeControls(hwnd);
            return 0;
        }

        case WM_COMMAND: {
            // Handle accelerator for Ctrl+W
            if (LOWORD(wParam) == ID_ACCEL_QUIT && HIWORD(wParam) == 1) {
                if (!HandleCtrlW(hwnd)) {
                    // No child dialogs - post WM_CLOSE to trigger confirmation
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                return 0;
            }
            
            if (LOWORD(wParam) == ID_SEARCH_BTN) {
                // Open search dialog
                INT_PTR result = DialogBoxW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_SEARCH_DIALOG), hwnd, SearchDialogProc);
                if (result == IDOK) {
                    // User clicked Search - execute the search
                    ExecuteSearch();
                }
            }
            else if (LOWORD(wParam) == ID_END_SEARCH_BTN) {
                // End search and restore all data
                EndSearch();
            }
            else if (LOWORD(wParam) == ID_INSTALLED_BTN) {
                // Toggle installed filter
                bool wasActive = IsInstalledFilterActive();
                SetInstalledFilterActive(!wasActive);
                
                // Load installed package IDs if activating filter
                if (IsInstalledFilterActive()) {
                    LoadInstalledPackageIds(g_db);
                    // Run cleanup to remove uninstalled apps (fast registry check)
                    CleanupInstalledApps(g_db);
                }
                
                // Show spinner when deactivating (going back to all apps)
                if (wasActive && !IsInstalledFilterActive()) {
                    // Create and show loading dialog
                    g_hIconLoadingDialog = CreateDialog((HINSTANCE)GetWindowLongPtr(g_mainWindow, GWLP_HINSTANCE), 
                                                        MAKEINTRESOURCE(IDD_LOADING_DIALOG), g_mainWindow, IconLoadingDialogProc);
                    if (g_hIconLoadingDialog) {
                        SetDlgItemTextW(g_hIconLoadingDialog, IDC_LOADING_TEXT, g_locale.repopulating_table.c_str());
                        ShowWindow(g_hIconLoadingDialog, SW_SHOW);
                        UpdateWindow(g_hIconLoadingDialog);
                        
                        // Process messages to let spinner start
                        MSG msg;
                        for (int i = 0; i < 10; i++) {
                            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                                TranslateMessage(&msg);
                                DispatchMessageW(&msg);
                            }
                            Sleep(16);
                        }
                    }
                }
                
                InvalidateRect(g_hInstalledBtn, NULL, TRUE);  // Redraw button with new state
                LoadTags();  // Refresh categories to show only those with installed apps
                OnTagSelectionChanged();  // Refresh the app list
                
                // Update button visibility and layout
                ResizeControls(hwnd);
                
                // Destroy spinner dialog if it was shown
                if (g_hIconLoadingDialog) {
                    DestroyWindow(g_hIconLoadingDialog);
                    g_hIconLoadingDialog = nullptr;
                }
            }
            else if (LOWORD(wParam) == ID_REFRESH_INSTALLED_BTN) {
                // Run winget list to discover newly installed apps (slow)
                // Show spinner during operation
                g_hIconLoadingDialog = CreateDialog((HINSTANCE)GetWindowLongPtr(g_mainWindow, GWLP_HINSTANCE), 
                                                    MAKEINTRESOURCE(IDD_LOADING_DIALOG), g_mainWindow, IconLoadingDialogProc);
                if (g_hIconLoadingDialog) {
                    SetDlgItemTextW(g_hIconLoadingDialog, IDC_LOADING_TEXT, g_locale.querying_winget.c_str());
                    ShowWindow(g_hIconLoadingDialog, SW_SHOW);
                    UpdateWindow(g_hIconLoadingDialog);
                    
                    // Process messages to let spinner start
                    MSG msg;
                    for (int i = 0; i < 10; i++) {
                        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                            TranslateMessage(&msg);
                            DispatchMessageW(&msg);
                        }
                        Sleep(16);
                    }
                }
                
                // Run discovery in background thread
                std::thread([hwnd]() {
                    bool success = DiscoverInstalledApps(g_db);
                    
                    // Back on main thread, update UI
                    PostMessageW(hwnd, WM_USER + 2, success ? 1 : 0, 0);
                }).detach();
            }
            else if (LOWORD(wParam) == ID_ABOUT_BTN) {
                // Show About dialog
                ShowAboutDialog(hwnd);
            }
            else if (LOWORD(wParam) == ID_QUIT_BTN) {
                // Post WM_CLOSE which will show quit confirmation
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            else if (HIWORD(wParam) == CBN_SELCHANGE) {
                if ((HWND)lParam == g_hLangCombo) {
                    OnLanguageChanged();
                }
            }
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR nmhdr = (LPNMHDR)lParam;
            
            // Handle custom draw for both ListViews to add 3px text spacing
            if ((nmhdr->idFrom == ID_TAG_TREE || nmhdr->idFrom == ID_APP_LIST) && nmhdr->code == NM_CUSTOMDRAW) {
                LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
                
                switch (lplvcd->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYSUBITEMDRAW;
                    
                    case CDDS_ITEMPREPAINT:
                        return CDRF_NOTIFYSUBITEMDRAW;
                    
                    case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
                        // Add spacing before text - more for first column with icons
                        if (lplvcd->iSubItem == 0) {
                            lplvcd->nmcd.rc.left += 6;  // More space after icon
                        } else {
                            lplvcd->nmcd.rc.left += 3;  // Standard spacing for other columns
                        }
                        return CDRF_DODEFAULT;
                }
            }
            
            // Handle category list selection (now ListView instead of TreeView)
            if (nmhdr->idFrom == ID_TAG_TREE && nmhdr->code == LVN_ITEMCHANGED) {
                LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                // Only respond to state changes that involve selection
                if ((pnmv->uChanged & LVIF_STATE) && (pnmv->uNewState & LVIS_SELECTED)) {
                    OnTagSelectionChanged();
                }
            }
            else if (nmhdr->idFrom == ID_APP_LIST && nmhdr->code == NM_DBLCLK) {
                OnAppDoubleClick();
            }
            else if (nmhdr->idFrom == ID_APP_LIST && nmhdr->code == LVN_GETDISPINFOW) {
                NMLVDISPINFOW* pDispInfo = (NMLVDISPINFOW*)lParam;
                AppInfo* app = (AppInfo*)pDispInfo->item.lParam;
                
                if (pDispInfo->item.mask & LVIF_TEXT) {
                    switch (pDispInfo->item.iSubItem) {
                        case 0: // Name
                            pDispInfo->item.pszText = (LPWSTR)app->name.c_str();
                            break;
                        case 1: // Version
                            pDispInfo->item.pszText = (LPWSTR)app->version.c_str();
                            break;
                        case 2: // Publisher
                            pDispInfo->item.pszText = (LPWSTR)app->publisher.c_str();
                            break;
                    }
                }
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int xPos = GET_X_LPARAM(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            if (xPos >= g_splitterPos - 2 && xPos <= g_splitterPos + 2) {
                g_draggingSplitter = true;
                SetCapture(hwnd);
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_draggingSplitter) {
                g_draggingSplitter = false;
                ReleaseCapture();
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int xPos = GET_X_LPARAM(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            
            if (g_draggingSplitter) {
                int maxRight = rc.right - 400;
                g_splitterPos = std::max(200, std::min(xPos, maxRight));
                ResizeControls(hwnd);
            }
            else if (xPos >= g_splitterPos - 2 && xPos <= g_splitterPos + 2) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            }
            else {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            
            // Make count labels blue with proper background
            if (hStatic == g_hTagCountLabel || hStatic == g_hAppCountLabel) {
                SetTextColor(hdcStatic, RGB(0, 102, 204)); // Blue color
                SetBkMode(hdcStatic, OPAQUE);
                SetBkColor(hdcStatic, GetSysColor(COLOR_WINDOW));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            
            // Make category label navy blue with proper background
            if (hStatic == g_hCategoryLabel) {
                SetTextColor(hdcStatic, RGB(0, 0, 128)); // Navy blue
                SetBkMode(hdcStatic, OPAQUE);
                SetBkColor(hdcStatic, GetSysColor(COLOR_WINDOW));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlID == ID_SEARCH_BTN) {
                HWND hBtn = dis->hwndItem;
                BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                // Base dark-blue color and variations (matching WinUpdate About button)
                COLORREF base = RGB(10,57,129);
                COLORREF hoverCol = RGB(25,95,210);
                COLORREF pressCol = RGB(6,34,80);
                HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                // Draw text
                SetTextColor(hdc, RGB(255,255,255));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = g_hBoldFont ? g_hBoldFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HGDIOBJ oldf = SelectObject(hdc, hf);
                DrawTextW(hdc, g_locale.search_btn.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldf);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
                return TRUE;
            } else if (dis->CtlID == ID_END_SEARCH_BTN) {
                HWND hBtn = dis->hwndItem;
                BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                // Red color scheme for End Search
                COLORREF base = RGB(180,40,40);
                COLORREF hoverCol = RGB(220,60,60);
                COLORREF pressCol = RGB(120,20,20);
                HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                // Draw text
                SetTextColor(hdc, RGB(255,255,255));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = g_hBoldFont ? g_hBoldFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HGDIOBJ oldf = SelectObject(hdc, hf);
                DrawTextW(hdc, g_locale.end_search_btn.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldf);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
                return TRUE;
            } else if (dis->CtlID == ID_INSTALLED_BTN) {
                HWND hBtn = dis->hwndItem;
                BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                // Green color scheme when active, dark blue (matching Search) when inactive
                COLORREF base = IsInstalledFilterActive() ? RGB(40,140,40) : RGB(10,57,129);
                COLORREF hoverCol = IsInstalledFilterActive() ? RGB(60,180,60) : RGB(25,95,210);
                COLORREF pressCol = IsInstalledFilterActive() ? RGB(20,100,20) : RGB(6,34,80);
                HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                // Draw text with checkmark when active
                SetTextColor(hdc, RGB(255,255,255));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = g_hBoldFont ? g_hBoldFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HGDIOBJ oldf = SelectObject(hdc, hf);
                std::wstring btnText = IsInstalledFilterActive() ? (L"✓ " + g_locale.installed_btn) : g_locale.installed_btn;
                DrawTextW(hdc, btnText.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldf);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
                return TRUE;
            } else if (dis->CtlID == ID_REFRESH_INSTALLED_BTN) {
                HWND hBtn = dis->hwndItem;
                BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                // Blue color scheme like Search button
                COLORREF base = RGB(10,57,129);
                COLORREF hoverCol = RGB(40,90,170);
                COLORREF pressCol = RGB(5,40,90);
                HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                // Draw text
                SetTextColor(hdc, RGB(255,255,255));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = g_hBoldFont ? g_hBoldFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HGDIOBJ oldf = SelectObject(hdc, hf);
                DrawTextW(hdc, g_locale.refresh_installed_btn.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldf);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
                return TRUE;
            } else if (dis->CtlID == ID_ABOUT_BTN) {
                HWND hBtn = dis->hwndItem;
                BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                // Blue color scheme like Search button
                COLORREF base = RGB(10,57,129);
                COLORREF hoverCol = RGB(40,90,170);
                COLORREF pressCol = RGB(5,40,90);
                HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                // Draw text
                SetTextColor(hdc, RGB(255,255,255));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = g_hBoldFont ? g_hBoldFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HGDIOBJ oldf = SelectObject(hdc, hf);
                DrawTextW(hdc, g_locale.about_btn.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldf);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
                return TRUE;
            } else if (dis->CtlID == ID_QUIT_BTN) {
                HWND hBtn = dis->hwndItem;
                BOOL hover = (BOOL)GetWindowLongPtrW(hBtn, GWLP_USERDATA);
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                // Red color scheme for Quit button
                COLORREF base = RGB(180,40,40);
                COLORREF hoverCol = RGB(220,60,60);
                COLORREF pressCol = RGB(120,20,20);
                HBRUSH hBrush = CreateSolidBrush(pressed ? pressCol : (hover ? hoverCol : base));
                FillRect(hdc, &rc, hBrush);
                DeleteObject(hBrush);
                // Draw text
                SetTextColor(hdc, RGB(255,255,255));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = g_hBoldFont ? g_hBoldFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
                HGDIOBJ oldf = SelectObject(hdc, hf);
                DrawTextW(hdc, g_locale.quit_btn.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldf);
                if (dis->itemState & ODS_FOCUS) DrawFocusRect(hdc, &rc);
                return TRUE;
            }
            return FALSE;
        }
        
        case WM_CLOSE: {
            // Show quit confirmation dialog
            if (ShowQuitConfirmation(hwnd, g_locale.quit_title, g_locale.quit_message,
                                    g_locale.yes_btn, g_locale.no_btn)) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY:
            CloseDatabase();
            if (g_hFont) DeleteObject(g_hFont);
            if (g_hBoldFont) DeleteObject(g_hBoldFont);
            if (g_hImageList) ImageList_Destroy(g_hImageList);
            if (g_hTreeImageList) ImageList_Destroy(g_hTreeImageList);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

void CreateControls(HWND hwnd) {
    HINSTANCE hInst = GetModuleHandle(NULL);
    
    // Language selector combobox (top right)
    g_hLangCombo = CreateWindowExW(
        0,
        L"COMBOBOX",
        L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_LANG_COMBO,
        hInst,
        NULL
    );
    SendMessageW(g_hLangCombo, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"English (UK)");
    SendMessageW(g_hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Norsk (NB)");
    SendMessageW(g_hLangCombo, CB_ADDSTRING, 0, (LPARAM)L"Svenska");
    
    // Set current language
    if (g_currentLang == L"en_GB") SendMessageW(g_hLangCombo, CB_SETCURSEL, 0, 0);
    else if (g_currentLang == L"nb_NO") SendMessageW(g_hLangCombo, CB_SETCURSEL, 1, 0);
    else if (g_currentLang == L"sv_SE") SendMessageW(g_hLangCombo, CB_SETCURSEL, 2, 0);
    
    // Pump messages to keep spinner alive
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    // Search button (magnifying glass) - owner-draw for custom colors
    g_hSearchBtn = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_SEARCH_BTN,
        hInst,
        NULL
    );
    SetWindowTextW(g_hSearchBtn, g_locale.search_btn.c_str());
    SendMessageW(g_hSearchBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Subclass search button for hover effects (matching WinUpdate About button)
    if (g_hSearchBtn) {
        SetWindowSubclass(g_hSearchBtn, [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
            switch (msg) {
            case WM_MOUSEMOVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 1);
                InvalidateRect(h, NULL, TRUE);
                TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h; TrackMouseEvent(&tme);
                break;
            }
            case WM_MOUSELEAVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                InvalidateRect(h, NULL, TRUE);
                break;
            }
            }
            return DefSubclassProc(h, msg, wp, lp);
        }, 0, 0);
    }
    
    // End Search button (initially hidden) - owner-draw for custom colors
    g_hEndSearchBtn = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,  // Hidden by default
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_END_SEARCH_BTN,
        hInst,
        NULL
    );
    SetWindowTextW(g_hEndSearchBtn, g_locale.end_search_btn.c_str());
    SendMessageW(g_hEndSearchBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Subclass end search button for hover effects
    if (g_hEndSearchBtn) {
        SetWindowSubclass(g_hEndSearchBtn, [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
            switch (msg) {
            case WM_MOUSEMOVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 1);
                InvalidateRect(h, NULL, TRUE);
                TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h; TrackMouseEvent(&tme);
                break;
            }
            case WM_MOUSELEAVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                InvalidateRect(h, NULL, TRUE);
                break;
            }
            }
            return DefSubclassProc(h, msg, wp, lp);
        }, 0, 0);
    }
    
    // Installed button (toggle to show only installed apps)
    g_hInstalledBtn = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_INSTALLED_BTN,
        hInst,
        NULL
    );
    SetWindowTextW(g_hInstalledBtn, g_locale.installed_btn.c_str());
    SendMessageW(g_hInstalledBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Subclass installed button for hover effects
    if (g_hInstalledBtn) {
        SetWindowSubclass(g_hInstalledBtn, [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
            switch (msg) {
            case WM_MOUSEMOVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 1);
                InvalidateRect(h, NULL, TRUE);
                TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h; TrackMouseEvent(&tme);
                break;
            }
            case WM_MOUSELEAVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                InvalidateRect(h, NULL, TRUE);
                break;
            }
            }
            return DefSubclassProc(h, msg, wp, lp);
        }, 0, 0);
    }
    
    // Refresh Installed button (manual winget list discovery)
    g_hRefreshInstalledBtn = CreateWindowExW(
        0,
        L"BUTTON",
        L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_REFRESH_INSTALLED_BTN,
        hInst,
        NULL
    );
    SendMessageW(g_hRefreshInstalledBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Create custom tooltip window class (WinUpdate style)
    static const wchar_t *kTipClass = L"WPMSimpleTooltip";
    static bool tipRegistered = false;
    if (!tipRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hw, UINT um, WPARAM w, LPARAM l)->LRESULT {
            if (um == WM_PAINT) {
                PAINTSTRUCT ps; HDC hdc = BeginPaint(hw, &ps);
                RECT rc; GetClientRect(hw, &rc);
                // Yellow tooltip background
                FillRect(hdc, &rc, (HBRUSH)(COLOR_INFOBK + 1));
                FrameRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
                int len = GetWindowTextLengthW(hw);
                std::vector<wchar_t> buf(len + 1);
                if (len > 0) GetWindowTextW(hw, buf.data(), len + 1);
                SetTextColor(hdc, GetSysColor(COLOR_INFOTEXT));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = (HFONT)SendMessageW(hw, WM_GETFONT, 0, 0);
                HGDIOBJ old = NULL; if (hf) old = SelectObject(hdc, hf);
                RECT inner = rc; inner.left += 4; inner.right -= 4; inner.top += 2; inner.bottom -= 2;
                // Multi-line support with DT_WORDBREAK
                DrawTextW(hdc, buf.data(), -1, &inner, DT_LEFT | DT_WORDBREAK | DT_VCENTER);
                if (old) SelectObject(hdc, old);
                EndPaint(hw, &ps);
                return 0;
            }
            return DefWindowProcW(hw, um, w, l);
        };
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hbrBackground = (HBRUSH)(COLOR_INFOBK + 1);
        wc.lpszClassName = kTipClass;
        RegisterClassExW(&wc);
        tipRegistered = true;
    }
    
    // Create tooltip window - empty initially
    g_hRefreshTooltip = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kTipClass,
        L"",
        WS_POPUP,
        0, 0, 250, 50,  // Will be resized based on text
        hwnd,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );
    SendMessageW(g_hRefreshTooltip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    // Set text after creation
    SetWindowTextW(g_hRefreshTooltip, g_locale.refresh_installed_tooltip.c_str());
    SendMessageW(g_hRefreshTooltip, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Subclass refresh button for hover effects and tooltip
    if (g_hRefreshInstalledBtn && g_hRefreshTooltip) {
        SetWindowSubclass(g_hRefreshInstalledBtn, [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
            switch (msg) {
            case WM_MOUSEMOVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 1);
                InvalidateRect(h, NULL, TRUE);
                TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h; TrackMouseEvent(&tme);
                
                // Show tooltip above button (not below cursor)
                if (g_hRefreshTooltip && !IsWindowVisible(g_hRefreshTooltip)) {
                    // Get button rect
                    RECT btnRect;
                    GetWindowRect(h, &btnRect);
                    
                    // Calculate tooltip size based on text
                    HDC hdc = GetDC(g_hRefreshTooltip);
                    HFONT hFont = (HFONT)SendMessageW(g_hRefreshTooltip, WM_GETFONT, 0, 0);
                    HGDIOBJ oldFont = SelectObject(hdc, hFont);
                    RECT textRect = {0, 0, 250, 0};
                    DrawTextW(hdc, g_locale.refresh_installed_tooltip.c_str(), -1, &textRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK);
                    SelectObject(hdc, oldFont);
                    ReleaseDC(g_hRefreshTooltip, hdc);
                    
                    int tipWidth = textRect.right + 8;  // Add padding
                    int tipHeight = textRect.bottom + 4;
                    
                    // Position tooltip above button, centered
                    int tipX = btnRect.left + (btnRect.right - btnRect.left - tipWidth) / 2;
                    int tipY = btnRect.top - tipHeight - 5;  // 5px gap above button
                    
                    SetWindowPos(g_hRefreshTooltip, HWND_TOP, tipX, tipY, tipWidth, tipHeight, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                }
                break;
            }
            case WM_MOUSELEAVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                InvalidateRect(h, NULL, TRUE);
                // Hide tooltip
                if (g_hRefreshTooltip) {
                    ShowWindow(g_hRefreshTooltip, SW_HIDE);
                }
                break;
            }
            }
            return DefSubclassProc(h, msg, wp, lp);
        }, 0, 0);
    }
    
    // Quit button (far left at beginning of button row)
    g_hQuitBtn = CreateWindowExW(
        0,
        L"BUTTON",
        L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_QUIT_BTN,
        hInst,
        NULL
    );
    SetWindowTextW(g_hQuitBtn, g_locale.quit_btn.c_str());
    SendMessageW(g_hQuitBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Subclass Quit button for hover effects
    if (g_hQuitBtn) {
        SetWindowSubclass(g_hQuitBtn, [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
            switch (msg) {
            case WM_MOUSEMOVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 1);
                InvalidateRect(h, NULL, TRUE);
                TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h; TrackMouseEvent(&tme);
                break;
            }
            case WM_MOUSELEAVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                InvalidateRect(h, NULL, TRUE);
                break;
            }
            }
            return DefSubclassProc(h, msg, wp, lp);
        }, 0, 0);
    }
    
    // About button (far right after language dropdown)
    g_hAboutBtn = CreateWindowExW(
        0,
        L"BUTTON",
        g_locale.about_btn.c_str(),
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_ABOUT_BTN,
        hInst,
        NULL
    );
    SendMessageW(g_hAboutBtn, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Subclass About button for hover effects
    if (g_hAboutBtn) {
        SetWindowSubclass(g_hAboutBtn, [](HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)->LRESULT {
            switch (msg) {
            case WM_MOUSEMOVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 1);
                InvalidateRect(h, NULL, TRUE);
                TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h; TrackMouseEvent(&tme);
                break;
            }
            case WM_MOUSELEAVE: {
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
                InvalidateRect(h, NULL, TRUE);
                break;
            }
            }
            return DefSubclassProc(h, msg, wp, lp);
        }, 0, 0);
    }
    
    // Category count label
    g_hTagCountLabel = CreateWindowExW(
        0,
        L"STATIC",
        L"0 categories",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0,
        hwnd,
        NULL,
        hInst,
        NULL
    );
    SendMessageW(g_hTagCountLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Left panel - Category list (using ListView for full icon control)
    g_hTagTree = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_TAG_TREE,
        hInst,
        NULL
    );
    SendMessageW(g_hTagTree, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Create image list with yellow folder icons (25x19 for open, 21x19 for closed)
    g_hTreeImageList = ImageList_Create(25, 19, ILC_COLOR32 | ILC_MASK, 2, 2);
    
    HICON hClosedIcon = NULL;
    HICON hOpenIcon = NULL;
    
    // Try to load custom folder icons first (both at 25x19 - closed will have empty space on right)
    hClosedIcon = (HICON)LoadImageW(NULL, L"gr_folder.icon_closed.ico", IMAGE_ICON, 25, 19, LR_LOADFROMFILE);
    hOpenIcon = (HICON)LoadImageW(NULL, L"gr_folder.icon_open.ico", IMAGE_ICON, 25, 19, LR_LOADFROMFILE);
    
    // If custom icons not found, extract yellow folders from shell32.dll
    if (!hClosedIcon || !hOpenIcon) {
        // Get the shell32.dll path
        wchar_t shell32Path[MAX_PATH];
        GetSystemDirectoryW(shell32Path, MAX_PATH);
        wcscat_s(shell32Path, L"\\shell32.dll");
        
        // Try multiple icon indices to find yellow folders
        // In Windows, the classic yellow folder is usually at index 3 or 4
        if (!hClosedIcon) {
            HICON hLarge = NULL, hSmall = NULL;
            // Try index 3 first (classic closed folder) - 25x19 to match open folder
            if (ExtractIconExW(shell32Path, 3, &hLarge, &hSmall, 1) > 0 && hSmall) {
                hClosedIcon = (HICON)CopyImage(hSmall, IMAGE_ICON, 25, 19, 0);
                DestroyIcon(hSmall);
                if (hLarge) DestroyIcon(hLarge);
            }
        }
        
        if (!hOpenIcon) {
            HICON hLarge = NULL, hSmall = NULL;
            // Try index 4 (classic open folder) - 25x19 for open flap
            if (ExtractIconExW(shell32Path, 4, &hLarge, &hSmall, 1) > 0 && hSmall) {
                hOpenIcon = (HICON)CopyImage(hSmall, IMAGE_ICON, 25, 19, 0);
                DestroyIcon(hSmall);
                if (hLarge) DestroyIcon(hLarge);
            }
        }
    }
    
    // Add icons to image list - ensure BOTH icons are added at same size
    // If one is missing, use the available one for both
    if (!hClosedIcon && hOpenIcon) {
        hClosedIcon = (HICON)CopyImage(hOpenIcon, IMAGE_ICON, 25, 19, 0);
    }
    if (!hOpenIcon && hClosedIcon) {
        hOpenIcon = (HICON)CopyImage(hClosedIcon, IMAGE_ICON, 25, 19, 0);
    }
    
    if (hClosedIcon) {
        ImageList_AddIcon(g_hTreeImageList, hClosedIcon);  // Index 0: closed folder
        DestroyIcon(hClosedIcon);
    }
    
    if (hOpenIcon) {
        ImageList_AddIcon(g_hTreeImageList, hOpenIcon);  // Index 1: open folder
        DestroyIcon(hOpenIcon);
    }
    
    ListView_SetImageList(g_hTagTree, g_hTreeImageList, LVSIL_SMALL);
    
    // Add a single column that will take the full width
    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH | LVCF_FMT | LVCF_MINWIDTH;
    col.cx = 10000;  // Very wide to fill available space
    col.cxMin = 50;  // Minimum width to ensure spacing
    col.fmt = LVCFMT_LEFT | LVCFMT_FIXED_WIDTH;  // Left align with fixed width
    ListView_InsertColumn(g_hTagTree, 0, &col);
    
    // Set left margin for items (4px padding)
    RECT rcItem;
    ListView_GetItemRect(g_hTagTree, 0, &rcItem, LVIR_BOUNDS);
    ListView_SetColumnWidth(g_hTagTree, 0, 10000);
    
    // Set extended styles to add some spacing
    DWORD dwExStyle = ListView_GetExtendedListViewStyle(g_hTagTree);
    ListView_SetExtendedListViewStyle(g_hTagTree, dwExStyle | LVS_EX_DOUBLEBUFFER);
    
    // Manually adjust icon position by setting item indent
    // We'll do this when inserting items
    
    // Pump messages to keep spinner alive
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    // Category name label (bold navy blue)
    g_hCategoryLabel = CreateWindowExW(
        0,
        L"STATIC",
        L"All",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0,
        hwnd,
        NULL,
        hInst,
        NULL
    );
    SendMessageW(g_hCategoryLabel, WM_SETFONT, (WPARAM)g_hBoldFont, TRUE);
    
    // App count label
    g_hAppCountLabel = CreateWindowExW(
        0,
        L"STATIC",
        L"0 apps",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        0, 0, 0, 0,
        hwnd,
        NULL,
        hInst,
        NULL
    );
    SendMessageW(g_hAppCountLabel, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Right panel - App list
    g_hAppList = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_APP_LIST,
        hInst,
        NULL
    );
    SendMessageW(g_hAppList, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    ListView_SetExtendedListViewStyle(g_hAppList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    
    // Create image list for icons and add a default application icon
    g_hImageList = ImageList_Create(21, 19, ILC_COLOR32 | ILC_MASK, 100, 100);
    
    // Create a blue dot icon as default (for apps without icons)
    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, 21, 19);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    
    // Fill with transparent background
    HBRUSH hBrushBg = CreateSolidBrush(RGB(240, 240, 240));
    RECT rect = {0, 0, 21, 19};
    FillRect(hdcMem, &rect, hBrushBg);
    DeleteObject(hBrushBg);
    
    // Draw 3D brown package/box icon (scaled to 19x19)
    // Front face (main brown)
    HBRUSH hBrownFront = CreateSolidBrush(RGB(139, 90, 43));
    HPEN hPenBrown = CreatePen(PS_SOLID, 1, RGB(101, 67, 33));
    HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPenBrown);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdcMem, hBrownFront);
    
    // Draw front face (coordinates scaled by 1.2)
    POINT frontFace[] = {{4, 7}, {4, 16}, {12, 16}, {12, 7}};
    Polygon(hdcMem, frontFace, 4);
    
    // Top face (lighter brown for highlight)
    HBRUSH hBrownTop = CreateSolidBrush(RGB(180, 120, 60));
    SelectObject(hdcMem, hBrownTop);
    POINT topFace[] = {{4, 7}, {7, 4}, {15, 4}, {12, 7}};
    Polygon(hdcMem, topFace, 4);
    
    // Right side face (darker brown for shadow)
    HBRUSH hBrownSide = CreateSolidBrush(RGB(101, 67, 33));
    SelectObject(hdcMem, hBrownSide);
    POINT sideFace[] = {{12, 7}, {15, 4}, {15, 12}, {12, 16}};
    Polygon(hdcMem, sideFace, 4);
    
    // Draw tape line across top
    HPEN hPenTape = CreatePen(PS_SOLID, 1, RGB(210, 180, 140));
    SelectObject(hdcMem, hPenTape);
    MoveToEx(hdcMem, 5, 5, NULL);
    LineTo(hdcMem, 14, 5);
    
    SelectObject(hdcMem, hOldBrush);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hBrownFront);
    DeleteObject(hBrownTop);
    DeleteObject(hBrownSide);
    DeleteObject(hPenBrown);
    DeleteObject(hPenTape);
    
    SelectObject(hdcMem, hOldBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);
    
    ImageList_Add(g_hImageList, hBitmap, NULL);
    DeleteObject(hBitmap);
    
    ListView_SetImageList(g_hAppList, g_hImageList, LVSIL_SMALL);
    
    // Pump messages to keep spinner alive
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    LVCOLUMNW appCol = {};
    appCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    appCol.fmt = LVCFMT_LEFT;
    
    appCol.pszText = (LPWSTR)L"Name";
    appCol.cx = 310;
    ListView_InsertColumn(g_hAppList, 0, &appCol);
    
    appCol.pszText = (LPWSTR)L"Version";
    appCol.cx = 150;
    ListView_InsertColumn(g_hAppList, 1, &appCol);
    
    appCol.pszText = (LPWSTR)L"Publisher";
    appCol.cx = 260;  // 5% wider
    ListView_InsertColumn(g_hAppList, 2, &appCol);
}

void ResizeControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    const int margin = 8;
    const int btnHeight = 30;
    const int labelHeight = 24;
    const int spacing = 4;
    const int langComboWidth = 150;
    const int aboutBtnWidth = 70;
    const int quitBtnWidth = 70;
    const int searchBtnWidth = 100;
    const int installedBtnWidth = 100;
    const int refreshInstalledBtnWidth = 180;
    const int endSearchBtnWidth = 110;
    const int searchBtnSpacing = 9;  // Extra space between buttons and dropdown
    
    // Quit button (far left)
    MoveWindow(g_hQuitBtn, margin, margin, quitBtnWidth, btnHeight, TRUE);
    
    // Language selector and About button (top right) - About is at far right
    MoveWindow(g_hLangCombo, rc.right - margin - langComboWidth - spacing - aboutBtnWidth, margin, langComboWidth, 200, TRUE);
    MoveWindow(g_hAboutBtn, rc.right - margin - aboutBtnWidth, margin, aboutBtnWidth, btnHeight, TRUE);
    
    // Button layout depends on search state and installed filter state
    // From right: [About] [Language] ... [End Search?] [Search] [Installed] [Refresh Installed (if active)]
    
    // Determine if Refresh Installed button should be visible
    bool showRefreshInstalled = IsInstalledFilterActive();
    if (g_hRefreshInstalledBtn) {
        ShowWindow(g_hRefreshInstalledBtn, showRefreshInstalled ? SW_SHOW : SW_HIDE);
    }
    
    if (g_searchActive && IsWindowVisible(g_hEndSearchBtn)) {
        // All buttons visible: [Refresh Installed?] [Installed] [Search] [End Search] ... [Language] [About]
        if (showRefreshInstalled) {
            int totalBtnWidth = refreshInstalledBtnWidth + spacing + installedBtnWidth + spacing + searchBtnWidth + spacing + endSearchBtnWidth;
            int refreshInstalledX = rc.right - margin - aboutBtnWidth - spacing - langComboWidth - searchBtnSpacing - totalBtnWidth;
            int installedX = refreshInstalledX + refreshInstalledBtnWidth + spacing;
            int searchX = installedX + installedBtnWidth + spacing;
            int endSearchX = searchX + searchBtnWidth + spacing;
            
            MoveWindow(g_hRefreshInstalledBtn, refreshInstalledX, margin, refreshInstalledBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hInstalledBtn, installedX, margin, installedBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hSearchBtn, searchX, margin, searchBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hEndSearchBtn, endSearchX, margin, endSearchBtnWidth, btnHeight, TRUE);
        } else {
            int totalBtnWidth = installedBtnWidth + spacing + searchBtnWidth + spacing + endSearchBtnWidth;
            int installedX = rc.right - margin - aboutBtnWidth - spacing - langComboWidth - searchBtnSpacing - totalBtnWidth;
            int searchX = installedX + installedBtnWidth + spacing;
            int endSearchX = searchX + searchBtnWidth + spacing;
            
            MoveWindow(g_hInstalledBtn, installedX, margin, installedBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hSearchBtn, searchX, margin, searchBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hEndSearchBtn, endSearchX, margin, endSearchBtnWidth, btnHeight, TRUE);
        }
    } else {
        // Search, Installed, and maybe Refresh Installed buttons visible
        if (showRefreshInstalled) {
            int totalBtnWidth = refreshInstalledBtnWidth + spacing + installedBtnWidth + spacing + searchBtnWidth;
            int refreshInstalledX = rc.right - margin - aboutBtnWidth - spacing - langComboWidth - searchBtnSpacing - totalBtnWidth;
            int installedX = refreshInstalledX + refreshInstalledBtnWidth + spacing;
            int searchX = installedX + installedBtnWidth + spacing;
            
            MoveWindow(g_hRefreshInstalledBtn, refreshInstalledX, margin, refreshInstalledBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hInstalledBtn, installedX, margin, installedBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hSearchBtn, searchX, margin, searchBtnWidth, btnHeight, TRUE);
        } else {
            int totalBtnWidth = installedBtnWidth + spacing + searchBtnWidth;
            int installedX = rc.right - margin - aboutBtnWidth - spacing - langComboWidth - searchBtnSpacing - totalBtnWidth;
            int searchX = installedX + installedBtnWidth + spacing;
            
            MoveWindow(g_hInstalledBtn, installedX, margin, installedBtnWidth, btnHeight, TRUE);
            MoveWindow(g_hSearchBtn, searchX, margin, searchBtnWidth, btnHeight, TRUE);
        }
    }
    
    // Left panel
    int leftWidth = g_splitterPos - margin;
    MoveWindow(g_hTagCountLabel, margin, margin + btnHeight + spacing, leftWidth, labelHeight, TRUE);
    MoveWindow(g_hTagTree, margin, margin + btnHeight + spacing + labelHeight + spacing, 
               leftWidth, rc.bottom - margin * 2 - btnHeight - labelHeight - spacing * 2, TRUE);
    
    // Right panel
    int rightX = g_splitterPos + 4;
    int rightWidth = rc.right - rightX - margin - langComboWidth - spacing;
    
    // Reserve space for count (e.g., "14,109 apps" = ~100px)
    const int countWidth = 100;
    const int categoryPadding = 5;
    int categoryWidth = rightWidth - countWidth - spacing - categoryPadding;
    
    MoveWindow(g_hCategoryLabel, rightX + categoryPadding, margin + btnHeight + spacing, categoryWidth, labelHeight, TRUE);
    MoveWindow(g_hAppCountLabel, rightX + categoryWidth + spacing, margin + btnHeight + spacing, countWidth, labelHeight, TRUE);
    
    // App list starts right after labels
    int appListY = margin + btnHeight + spacing + labelHeight + spacing;
    
    MoveWindow(g_hAppList, rightX, appListY,
               rc.right - rightX - margin, rc.bottom - appListY - margin, TRUE);
}

bool OpenDatabase() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    std::wstring dbPath = exePath;
    size_t lastSlash = dbPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        dbPath = dbPath.substr(0, lastSlash + 1);
    }
    dbPath += L"WinProgramManager.db";
    
    // Convert to UTF-8
    int size = WideCharToMultiByte(CP_UTF8, 0, dbPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string dbPathUtf8(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, dbPath.c_str(), -1, &dbPathUtf8[0], size, nullptr, nullptr);
    
    int result = sqlite3_open(dbPathUtf8.c_str(), &g_db);
    if (result != SQLITE_OK) {
        std::wstring msg = L"Failed to open database at:\n" + dbPath + L"\n\nError: " + 
                           std::wstring(sqlite3_errmsg(g_db), sqlite3_errmsg(g_db) + strlen(sqlite3_errmsg(g_db)));
        MessageBoxW(NULL, msg.c_str(), g_locale.database_error_title.c_str(), MB_ICONERROR | MB_OK);
        return false;
    }
    
    // Test query to verify database has data
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(g_db, "SELECT COUNT(*) FROM apps;", -1, &stmt, nullptr) != SQLITE_OK) {
        std::wstring msg = L"Database opened but query failed:\n";
        const char* err = sqlite3_errmsg(g_db);
        int wsize = MultiByteToWideChar(CP_UTF8, 0, err, -1, nullptr, 0);
        std::wstring errMsg(wsize - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, err, -1, &errMsg[0], wsize);
        msg += errMsg;
        MessageBoxW(NULL, msg.c_str(), g_locale.database_error_title.c_str(), MB_ICONERROR | MB_OK);
        return false;
    }
    sqlite3_finalize(stmt);
    
    // Load all data into memory for fast searching
    LoadAllDataIntoMemory();
    
    return true;
}

void CloseDatabase() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

// Installed apps functionality now in installed_apps.cpp

void LoadAllDataIntoMemory() {
    if (!g_db) return;
    
    // Clear existing data
    g_allApps.clear();
    g_allCategoryNames.clear();
    g_categoryToAppIds.clear();
    
    // Load all apps metadata (icons loaded separately after ImageList is created)
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, package_id, name, version, publisher, homepage FROM apps WHERE name IS NOT NULL AND TRIM(name) != '' ORDER BY name;";
    
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AppInfo app;
            app.id = sqlite3_column_int(stmt, 0);
            app.iconIndex = 0;  // Default to brown package icon (index 0)
            
            // Convert UTF-8 to wide strings
            auto convert = [](const char* utf8) -> std::wstring {
                if (!utf8 || !utf8[0]) return L"";
                
                int wsize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
                if (wsize == 0) {
                    wsize = MultiByteToWideChar(CP_ACP, 0, utf8, -1, nullptr, 0);
                    if (wsize == 0) return L"";
                    std::wstring result(wsize - 1, 0);
                    MultiByteToWideChar(CP_ACP, 0, utf8, -1, &result[0], wsize);
                    return result;
                }
                std::wstring result(wsize - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], wsize);
                return result;
            };
            
            app.packageId = convert((const char*)sqlite3_column_text(stmt, 1));
            app.name = convert((const char*)sqlite3_column_text(stmt, 2));
            app.version = convert((const char*)sqlite3_column_text(stmt, 3));
            app.publisher = convert((const char*)sqlite3_column_text(stmt, 4));
            app.homepage = convert((const char*)sqlite3_column_text(stmt, 5));
            
            g_allApps.push_back(app);
        }
        sqlite3_finalize(stmt);
    }
    
    // Load all categories and build app-category associations
    const char* catSql = "SELECT c.category_name, ac.app_id FROM categories c " 
                         "JOIN app_categories ac ON c.id = ac.category_id " 
                         "ORDER BY c.category_name;";
    
    std::set<std::wstring> uniqueCategories;
    
    if (sqlite3_prepare_v2(g_db, catSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* catName = (const char*)sqlite3_column_text(stmt, 0);
            int appId = sqlite3_column_int(stmt, 1);
            
            if (!catName || !catName[0]) continue;
            
            // Convert to wide string
            int wsize = MultiByteToWideChar(CP_UTF8, 0, catName, -1, nullptr, 0);
            if (wsize <= 1) continue;
            std::wstring categoryName(wsize - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, catName, -1, &categoryName[0], wsize);
            
            // Trim and capitalize
            size_t first = categoryName.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos) continue;
            size_t last = categoryName.find_last_not_of(L" \t\r\n");
            categoryName = categoryName.substr(first, last - first + 1);
            categoryName = CapitalizeFirst(categoryName);
            
            uniqueCategories.insert(categoryName);
            g_categoryToAppIds[categoryName].push_back(appId);
            
            // Add category to the app's categories list
            for (auto& app : g_allApps) {
                if (app.id == appId) {
                    app.categories.push_back(categoryName);
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Convert set to vector for easier access
    g_allCategoryNames.assign(uniqueCategories.begin(), uniqueCategories.end());
}

// Load all app icons into ImageList (must be called after ImageList is created)
// Dialog procedure for icon loading dialog
INT_PTR CALLBACK IconLoadingDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static int spinnerFrame = 0;
    static HWND hSpinner = NULL;
    static HWND hText = NULL;
    static HFONT hSpinnerFont = NULL;
    static HBRUSH hDialogBrush = NULL;
    
    switch (message) {
        case WM_INITDIALOG: {
            // Get the dialog background brush
            hDialogBrush = GetSysColorBrush(COLOR_BTNFACE);
            
            // Center the dialog on screen
            RECT rc;
            GetWindowRect(hDlg, &rc);
            int x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
            int y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
            SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
            
            // Set localized title
            SetWindowTextW(hDlg, g_locale.wait_moment.c_str());
            
            // Set info icon (blue I)
            HICON hIcon = LoadIcon(NULL, IDI_INFORMATION);
            SendDlgItemMessage(hDlg, IDC_LOADING_ICON, STM_SETICON, (WPARAM)hIcon, 0);
            
            // Get text control and set bold font
            hText = GetDlgItem(hDlg, IDC_LOADING_TEXT);
            if (hText) {
                HFONT hTextFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SendMessageW(hText, WM_SETFONT, (WPARAM)hTextFont, TRUE);
            }
            
            // Get spinner control and set up large font (matches startup spinner)
            hSpinner = GetDlgItem(hDlg, IDC_LOADING_ANIMATE);
            if (hSpinner) {
                hSpinnerFont = CreateFontW(60, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
                SendMessageW(hSpinner, WM_SETFONT, (WPARAM)hSpinnerFont, TRUE);
                // Set initial spinner character
                SetWindowTextW(hSpinner, L"\u25D0");
            }
            
            // Start timer for spinner animation (60ms = same as startup spinner)
            SetTimer(hDlg, 1, 60, NULL);
            spinnerFrame = 0;
            
            return TRUE;
        }
        
        case WM_TIMER: {
            if (wParam == 1 && hSpinner && IsWindow(hSpinner)) {
                const wchar_t* frames[] = { L"\u25D0", L"\u25D3", L"\u25D1", L"\u25D2" };
                spinnerFrame = (spinnerFrame + 1) % 4;
                SetWindowTextW(hSpinner, frames[spinnerFrame]);
                // Force immediate redraw
                InvalidateRect(hSpinner, NULL, FALSE);
                UpdateWindow(hSpinner);
            }
            return TRUE;
        }
        
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hStatic = (HWND)lParam;
            
            // Make spinner blue with transparent background
            if (hStatic == hSpinner) {
                SetTextColor(hdcStatic, RGB(0, 120, 215));
                SetBkMode(hdcStatic, TRANSPARENT);
                return (LRESULT)hDialogBrush;
            }
            // Regular text black with transparent background
            else if (hStatic == hText) {
                SetTextColor(hdcStatic, RGB(0, 0, 0));
                SetBkMode(hdcStatic, TRANSPARENT);
                return (LRESULT)hDialogBrush;
            }
            break;
        }
        
        case WM_DESTROY:
            KillTimer(hDlg, 1);
            if (hSpinnerFont) {
                DeleteObject(hSpinnerFont);
                hSpinnerFont = NULL;
            }
            return TRUE;
            
        case WM_CLOSE:
            return TRUE;  // Don't allow user to close
    }
    return FALSE;
}

void LoadAllIcons() {
    if (!g_db || !g_hImageList) return;
    
    // NOTE: Index 0 in ImageList is the brown package icon (default for apps without icons)
    // We must preserve this by starting custom icons at index 1+
    
    // Query all apps with their icons
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, icon_data FROM apps WHERE name IS NOT NULL AND TRIM(name) != '' ORDER BY name;";
    
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int appId = sqlite3_column_int(stmt, 0);
            
            // Find this app in g_allApps
            for (auto& app : g_allApps) {
                if (app.id == appId) {
                    // Try to load icon from database
                    if (sqlite3_column_type(stmt, 1) == SQLITE_BLOB) {
                        const void* blobData = sqlite3_column_blob(stmt, 1);
                        int blobSize = sqlite3_column_bytes(stmt, 1);
                        
                        if (blobData && blobSize > 0) {
                            HICON hIcon = LoadIconFromMemory((const unsigned char*)blobData, blobSize);
                            if (hIcon) {
                                // Add icon to ImageList - this returns new index (1, 2, 3, ...)
                                app.iconIndex = ImageList_AddIcon(g_hImageList, hIcon);
                                DestroyIcon(hIcon);
                            } else {
                                // Icon data exists but failed to load - use brown package
                                app.iconIndex = 0;
                            }
                        } else {
                            // No icon data - use brown package icon (index 0)
                            app.iconIndex = 0;
                        }
                    } else {
                        // No icon in database - use brown package icon (index 0)
                        app.iconIndex = 0;
                    }
                    break;
                }
            }
        }
        sqlite3_finalize(stmt);
    }
}

// All old dialog code removed - new dialog is created in WinMain before main window

std::wstring CapitalizeFirst(const std::wstring& str) {
    if (str.empty()) return str;
    std::wstring result = str;
    result[0] = towupper(result[0]);
    return result;
}

void LoadTags(const std::wstring& filter) {
    ListView_DeleteAllItems(g_hTagTree);
    
    // Clear old text buffers
    for (auto* buf : g_tagTextBuffers) {
        delete buf;
    }
    g_tagTextBuffers.clear();
    
    if (g_allCategoryNames.empty()) return;  // No data loaded
    
    // Add "All" item - use persistent storage
    std::wstring* allText = new std::wstring(L"   " + g_locale.all);
    g_tagTextBuffers.push_back(allText);
    
    LVITEMW lvi = {};
    lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
    lvi.iItem = 0;
    lvi.iSubItem = 0;
    lvi.pszText = (LPWSTR)allText->c_str();
    lvi.lParam = 0; // 0 = All
    lvi.iImage = 1; // Open folder (since it will be selected)
    int allIndex = ListView_InsertItem(g_hTagTree, &lvi);
    ListView_SetItemState(g_hTagTree, allIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    
    // Load categories from in-memory cache
    int itemIndex = 1;
    int processedCount = 0;
    for (const auto& categoryName : g_allCategoryNames) {
        // Process messages every 10 items to keep dialog responsive
        if (g_hIconLoadingDialog && (++processedCount % 10 == 0)) {
            ProcessDialogMessages();
        }
        
        // Get app count for this category (show all with 4+ apps, or categories where apps would be orphaned)
        auto it = g_categoryToAppIds.find(categoryName);
        if (it == g_categoryToAppIds.end() || it->second.empty()) continue;
        
        int appCount = it->second.size();
        bool hasOrphanedApps = false;
        
        // Check if category has apps that would be orphaned (only in this one category)
        if (appCount < 4) {
            for (int appId : it->second) {
                // Find this app in g_allApps and check how many categories it belongs to
                for (const auto& app : g_allApps) {
                    if (app.id == appId) {
                        if (app.categories.size() == 1) {
                            hasOrphanedApps = true;
                            break;
                        }
                        break;
                    }
                }
                if (hasOrphanedApps) break;
            }
        }
        
        // Only show categories with 4+ apps, or categories with orphaned apps
        if (appCount < 4 && !hasOrphanedApps) continue;
        
        // If installed filter is active, check if category has any installed apps
        if (IsInstalledFilterActive()) {
            bool hasInstalledApp = false;
            for (int appId : it->second) {
                // Find this app in g_allApps
                for (const auto& app : g_allApps) {
                    if (app.id == appId) {
                        if (IsPackageInstalled(app.packageId)) {
                            hasInstalledApp = true;
                            break;
                        }
                        break;
                    }
                }
                if (hasInstalledApp) break;
            }
            if (!hasInstalledApp) continue;  // Skip categories with no installed apps
        }
        
        // Apply filter if specified
        if (!filter.empty()) {
            std::wstring lower_name = categoryName;
            std::wstring lower_filter = filter;
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);
            std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::towlower);
            
            if (lower_name.find(lower_filter) == std::wstring::npos) {
                continue;
            }
        }
        
        // Store in persistent buffer for ListView to reference
        std::wstring* displayText = new std::wstring(L"   " + categoryName);
        g_tagTextBuffers.push_back(displayText);
        
        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem = itemIndex++;
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)displayText->c_str();
        lvi.lParam = (LPARAM)displayText; // Store pointer to tag name
        lvi.iImage = 0; // Closed folder
        ListView_InsertItem(g_hTagTree, &lvi);
    }
    
    // Update category count label
    int categoryCount = ListView_GetItemCount(g_hTagTree) - 1; // -1 for "All"
    // First clear with spaces
    SetWindowTextW(g_hTagCountLabel, L"                                        ");
    // Then set actual text
    std::wstring countText = FormatNumber(categoryCount) + L" " + g_locale.categories;
    SetWindowTextW(g_hTagCountLabel, countText.c_str());
    
    ListView_SetItemState(g_hTagTree, allIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void LoadApps(const std::wstring& tag, const std::wstring& filter) {
    ListView_DeleteAllItems(g_hAppList);
    
    if (g_allApps.empty()) return;  // No data loaded
    
    int appCount = 0;
    int index = 0;
    int processedCount = 0;
    
    // Filter apps from in-memory cache
    for (const auto& app : g_allApps) {
        // Process messages every 50 items to keep dialog responsive
        if (g_hIconLoadingDialog && (++processedCount % 50 == 0)) {
            ProcessDialogMessages();
        }
        
        // Check if app belongs to selected category
        bool categoryMatch = false;
        if (tag == L"All") {
            categoryMatch = true;
        } else {
            // Check if app has this category
            for (const auto& cat : app.categories) {
                if (cat == tag) {
                    categoryMatch = true;
                    break;
                }
            }
        }
        
        if (!categoryMatch) continue;
        
        // Apply filter if specified
        if (!filter.empty()) {
            std::wstring lower_name = app.name;
            std::wstring lower_publisher = app.publisher;
            std::wstring lower_packageId = app.packageId;
            std::wstring lower_filter = filter;
            
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);
            std::transform(lower_publisher.begin(), lower_publisher.end(), lower_publisher.begin(), ::towlower);
            std::transform(lower_packageId.begin(), lower_packageId.end(), lower_packageId.begin(), ::towlower);
            std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::towlower);
            
            if (lower_name.find(lower_filter) == std::wstring::npos &&
                lower_publisher.find(lower_filter) == std::wstring::npos &&
                lower_packageId.find(lower_filter) == std::wstring::npos) {
                continue;
            }
        }
        
        // Apply installed filter if active
        if (IsInstalledFilterActive()) {
            if (!IsPackageInstalled(app.packageId)) {
                continue;  // Skip non-installed apps
            }
        }
        
        // Create a copy of the app for ListView (using new to persist beyond this function)
        AppInfo* appCopy = new AppInfo(app);
        
        // Add to list view
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
        lvi.iItem = index++;
        lvi.iSubItem = 0;
        // Add spaces before name for spacing from icon
        std::wstring displayName = L"   " + appCopy->name;
        lvi.pszText = (LPWSTR)displayName.c_str();
        lvi.lParam = (LPARAM)appCopy;
        // Use iconIndex if valid, otherwise use 0 (brown package icon)
        lvi.iImage = (appCopy->iconIndex >= 0) ? appCopy->iconIndex : 0;
        ListView_InsertItem(g_hAppList, &lvi);
        
        ListView_SetItemText(g_hAppList, lvi.iItem, 1, (LPWSTR)appCopy->version.c_str());
        ListView_SetItemText(g_hAppList, lvi.iItem, 2, (LPWSTR)appCopy->publisher.c_str());
        
        appCount++;
    }
    
    // Update app count label
    SetWindowTextW(g_hAppCountLabel, L"                                        ");
    std::wstring countText = FormatNumber(appCount) + L" " + g_locale.apps;
    SetWindowTextW(g_hAppCountLabel, countText.c_str());
}

void OnTagSelectionChanged() {
    int selectedIndex = ListView_GetNextItem(g_hTagTree, -1, LVNI_SELECTED);
    if (selectedIndex == -1) return;
    
    // Set ALL items to closed folder icon
    int itemCount = ListView_GetItemCount(g_hTagTree);
    for (int i = 0; i < itemCount; i++) {
        LVITEMW item = {};
        item.mask = LVIF_IMAGE;
        item.iItem = i;
        item.iImage = 0; // Closed folder
        ListView_SetItem(g_hTagTree, &item);
    }
    
    // Set selected item to open folder icon AND get category name
    LVITEMW item = {};
    item.mask = LVIF_IMAGE | LVIF_PARAM;
    item.iItem = selectedIndex;
    ListView_GetItem(g_hTagTree, &item);  // First GET the item to retrieve lParam
    
    item.iImage = 1; // Open folder
    ListView_SetItem(g_hTagTree, &item);  // Then SET the icon
    
    // Determine category name
    if (item.lParam == 0) {
        g_selectedTag = L"All";
    } else {
        // Get the category name and trim the leading spaces added for display
        std::wstring displayName = *(std::wstring*)item.lParam;
        g_selectedTag = displayName;
        // Remove the 3 leading spaces we added for display
        if (g_selectedTag.length() >= 3 && g_selectedTag.substr(0, 3) == L"   ") {
            g_selectedTag = g_selectedTag.substr(3);
        }
    }
    
    // Update category label (show with spaces)
    if (item.lParam == 0) {
        SetWindowTextW(g_hCategoryLabel, g_locale.all.c_str());
    } else {
        SetWindowTextW(g_hCategoryLabel, (*(std::wstring*)item.lParam).c_str());
    }
    
    // Load apps for selected category (no filter)
    LoadApps(g_selectedTag, L"");
}

void OnAppDoubleClick() {
    int index = ListView_GetNextItem(g_hAppList, -1, LVNI_SELECTED);
    if (index == -1) return;
    
    LVITEMW lvi = {};
    lvi.mask = LVIF_PARAM | LVIF_IMAGE;
    lvi.iItem = index;
    ListView_GetItem(g_hAppList, &lvi);
    
    AppInfo* app = (AppInfo*)lvi.lParam;
    if (app) {
        // Get the icon from the image list and scale it up for the details dialog
        HICON hIcon = nullptr;
        if (lvi.iImage >= 0) {
            // Extract the small icon from image list
            HICON hSmallIcon = ImageList_GetIcon(g_hImageList, lvi.iImage, ILD_NORMAL);
            if (hSmallIcon) {
                // Create a 128x128 icon by scaling up
                // Get icon info
                ICONINFO iconInfo;
                if (GetIconInfo(hSmallIcon, &iconInfo)) {
                    // Create DCs for scaling
                    HDC hdcScreen = GetDC(NULL);
                    HDC hdcMem = CreateCompatibleDC(hdcScreen);
                    HDC hdcMemSrc = CreateCompatibleDC(hdcScreen);
                    
                    // Create 50x50 bitmap
                    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, 50, 50);
                    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBitmap);
                    HBITMAP hOldSrc = (HBITMAP)SelectObject(hdcMemSrc, iconInfo.hbmColor);
                    
                    // Fill with transparent background
                    HBRUSH hBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
                    RECT rc = {0, 0, 50, 50};
                    FillRect(hdcMem, &rc, hBrush);
                    
                    // Scale and draw
                    SetStretchBltMode(hdcMem, HALFTONE);
                    StretchBlt(hdcMem, 0, 0, 50, 50, hdcMemSrc, 0, 0, 21, 19, SRCCOPY);
                    
                    // Create mask
                    SelectObject(hdcMemSrc, iconInfo.hbmMask);
                    HBITMAP hMask = CreateCompatibleBitmap(hdcScreen, 50, 50);
                    SelectObject(hdcMem, hMask);
                    StretchBlt(hdcMem, 0, 0, 50, 50, hdcMemSrc, 0, 0, 21, 19, SRCCOPY);
                    
                    // Create the large icon
                    SelectObject(hdcMem, hBitmap);
                    ICONINFO newIconInfo;
                    newIconInfo.fIcon = TRUE;
                    newIconInfo.xHotspot = 0;
                    newIconInfo.yHotspot = 0;
                    newIconInfo.hbmMask = hMask;
                    newIconInfo.hbmColor = hBitmap;
                    hIcon = CreateIconIndirect(&newIconInfo);
                    
                    // Cleanup
                    SelectObject(hdcMem, hOldBmp);
                    SelectObject(hdcMemSrc, hOldSrc);
                    DeleteObject(hBitmap);
                    DeleteObject(hMask);
                    DeleteObject(iconInfo.hbmColor);
                    DeleteObject(iconInfo.hbmMask);
                    DeleteDC(hdcMem);
                    DeleteDC(hdcMemSrc);
                    ReleaseDC(NULL, hdcScreen);
                }
                DestroyIcon(hSmallIcon);
            }
        }
        
        // Show app details dialog with the scaled icon
        ShowAppDetailsDialog(g_mainWindow, g_db, app->packageId, hIcon);
        
        // Clean up scaled icon
        if (hIcon) {
            DestroyIcon(hIcon);
        }
    }
}

HBITMAP LoadIconFromBlob(const std::vector<unsigned char>& data, const std::wstring& type) {
    (void)data;
    (void)type;
    // Placeholder implementation - return NULL for now
    // TODO: Implement icon loading from BLOB
    return NULL;
}

HICON LoadIconFromMemory(const unsigned char* data, int size) {
    if (!data || size <= 0) return NULL;
    
    // Check if it's an ICO file (starts with 0x00 0x00 0x01 0x00)
    if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01 && data[3] == 0x00) {
        // Try to load as ICO
        int offset = LookupIconIdFromDirectoryEx((PBYTE)data, TRUE, 16, 16, LR_DEFAULTCOLOR);
        if (offset != 0 && offset < size) {
            HICON hIcon = CreateIconFromResourceEx((PBYTE)data + offset, size - offset, 
                                                   TRUE, 0x00030000, 16, 16, LR_DEFAULTCOLOR);
            if (hIcon) return hIcon;
        }
    }
    
    // Check if it's a PNG file (starts with 0x89 'P' 'N' 'G')
    if (size >= 8 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        // Try to load as PNG using IStream
        IStream* pStream = SHCreateMemStream(data, size);
        if (pStream) {
            // For now, we'll skip PNG support and return NULL
            // TODO: Implement PNG to HICON conversion using GDI+
            pStream->Release();
        }
    }
    
    // Return NULL if format not supported
    return NULL;
}

// Save configuration to INI file
void SaveConfig() {
    wchar_t* appDataPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath);
    
    if (FAILED(hr) || !appDataPath) {
        return;
    }
    
    std::wstring wAppDataPath(appDataPath);
    CoTaskMemFree(appDataPath);
    
    // Create WinProgramManager directory
    std::wstring baseDir = wAppDataPath + L"\\WinProgramManager";
    CreateDirectoryW(baseDir.c_str(), nullptr);
    
    std::wstring configFile = baseDir + L"\\WinProgramManager.ini";
    WritePrivateProfileStringW(L"Settings", L"Language", g_currentLang.c_str(), configFile.c_str());
}

// Load configuration from INI file
void LoadConfig() {
    wchar_t* appDataPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath);
    
    if (FAILED(hr) || !appDataPath) {
        return;
    }
    
    std::wstring wAppDataPath(appDataPath);
    CoTaskMemFree(appDataPath);
    
    std::wstring configFile = wAppDataPath + L"\\WinProgramManager\\WinProgramManager.ini";
    
    wchar_t langBuf[50] = {0};
    GetPrivateProfileStringW(L"Settings", L"Language", L"en_GB", langBuf, 50, configFile.c_str());
    g_currentLang = langBuf;
}

// Load locale from file
bool LoadLocale(const std::wstring& lang) {
    // Build file path
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exePath(path);
    size_t lastSlash = exePath.find_last_of(L"\\/");
    std::wstring localeFile = exePath.substr(0, lastSlash + 1) + L"locale\\" + lang + L".txt";

    // Read file as UTF-8 bytes
    std::ifstream file(localeFile.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read entire file into buffer
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string utf8Buffer(size, '\0');
    file.read(&utf8Buffer[0], size);
    file.close();

    // Convert UTF-8 to wide string
    int wideSize = MultiByteToWideChar(CP_UTF8, 0, utf8Buffer.c_str(), -1, NULL, 0);
    if (wideSize == 0) {
        return false;
    }
    std::wstring wideBuffer(wideSize, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Buffer.c_str(), -1, &wideBuffer[0], wideSize);

    // Parse lines
    std::wistringstream stream(wideBuffer);
    std::wstring line;
    while (std::getline(stream, line)) {
        // Skip comments, empty lines, and section headers
        if (line.empty() || line[0] == L'#' || line[0] == L'[') continue;

        // Parse key=value
        size_t eq = line.find(L'=');
        if (eq != std::wstring::npos) {
            std::wstring key = line.substr(0, eq);
            std::wstring value = line.substr(eq + 1);

            // Trim whitespace
            key = Trim(key);
            value = Trim(value);
            
            // Convert escaped \n to Windows line breaks \r\n
            size_t pos = 0;
            while ((pos = value.find(L"\\n", pos)) != std::wstring::npos) {
                value.replace(pos, 2, L"\r\n");
                pos += 2;  // Skip past the \r\n we just inserted
            }

            // Store in locale struct
            if (key == L"lang_name") g_locale.lang_name = value;
            else if (key == L"thousands_sep") g_locale.thousands_sep = value;
            else if (key == L"decimal_sep") g_locale.decimal_sep = value;
            else if (key == L"categories") g_locale.categories = value;
            else if (key == L"apps") g_locale.apps = value;
            else if (key == L"search_tags") g_locale.search_tags = value;
            else if (key == L"search_apps") g_locale.search_apps = value;
            else if (key == L"all") g_locale.all = value;
            else if (key == L"title") g_locale.title = value;
            else if (key == L"processing_database") g_locale.processing_database = value;
            else if (key == L"querying_winget") g_locale.querying_winget = value;
            else if (key == L"wait_moment") g_locale.wait_moment = value;
            else if (key == L"repopulating_table") g_locale.repopulating_table = value;
            else if (key == L"release_notes_title") g_locale.release_notes_title = value;
            else if (key == L"fetching_release_notes") g_locale.fetching_release_notes = value;
            else if (key == L"release_notes_unavailable") g_locale.release_notes_unavailable = value;
            else if (key == L"release_notes_fetch_error") g_locale.release_notes_fetch_error = value;
            else if (key == L"close") g_locale.close = value;
            // App Details Dialog
            else if (key == L"app_details_title") g_locale.app_details_title = value;
            else if (key == L"publisher_label") g_locale.publisher_label = value;
            else if (key == L"version_label") g_locale.version_label = value;
            else if (key == L"package_id_label") g_locale.package_id_label = value;
            else if (key == L"source_label") g_locale.source_label = value;
            else if (key == L"status_label") g_locale.status_label = value;
            else if (key == L"not_installed") g_locale.not_installed = value;
            else if (key == L"installed") g_locale.installed = value;
            else if (key == L"description_label") g_locale.description_label = value;
            else if (key == L"technical_info_label") g_locale.technical_info_label = value;
            else if (key == L"homepage_label") g_locale.homepage_label = value;
            else if (key == L"license_label") g_locale.license_label = value;
            else if (key == L"installer_type_label") g_locale.installer_type_label = value;
            else if (key == L"architecture_label") g_locale.architecture_label = value;
            else if (key == L"tags_label") g_locale.tags_label = value;
            else if (key == L"view_release_notes_btn") g_locale.view_release_notes_btn = value;
            else if (key == L"no_description") g_locale.no_description = value;
            else if (key == L"unknown") g_locale.unknown = value;
            else if (key == L"not_available") g_locale.not_available = value;
            else if (key == L"no_tags") g_locale.no_tags = value;
            else if (key == L"install_btn") g_locale.install_btn = value;
            else if (key == L"uninstall_btn") g_locale.uninstall_btn = value;
            else if (key == L"reinstall_btn") g_locale.reinstall_btn = value;
            else if (key == L"confirm_install_title") g_locale.confirm_install_title = value;
            else if (key == L"confirm_install_msg") g_locale.confirm_install_msg = value;
            else if (key == L"confirm_uninstall_title") g_locale.confirm_uninstall_title = value;
            else if (key == L"confirm_uninstall_msg") g_locale.confirm_uninstall_msg = value;
            else if (key == L"confirm_reinstall_title") g_locale.confirm_reinstall_title = value;
            else if (key == L"confirm_reinstall_msg") g_locale.confirm_reinstall_msg = value;
            else if (key == L"cancel_btn") g_locale.cancel_btn = value;
            else if (key == L"refresh_installed_btn") g_locale.refresh_installed_btn = value;
            else if (key == L"refresh_installed_tooltip") g_locale.refresh_installed_tooltip = value;
            else if (key == L"search_btn") g_locale.search_btn = value;
            else if (key == L"end_search_btn") g_locale.end_search_btn = value;
            else if (key == L"installed_btn") g_locale.installed_btn = value;
            // About Dialog
            else if (key == L"about_btn") g_locale.about_btn = value;
            else if (key == L"quit_btn") g_locale.quit_btn = value;
            else if (key == L"quit_title") g_locale.quit_title = value;
            else if (key == L"quit_message") g_locale.quit_message = value;
            else if (key == L"yes_btn") g_locale.yes_btn = value;
            else if (key == L"no_btn") g_locale.no_btn = value;
            else if (key == L"install_btn") g_locale.install_btn = value;
            else if (key == L"uninstall_btn") g_locale.uninstall_btn = value;
            else if (key == L"reinstall_btn") g_locale.reinstall_btn = value;
            // App Details Dialog
            else if (key == L"app_details_title") g_locale.app_details_title = value;
            else if (key == L"publisher_label") g_locale.publisher_label = value;
            else if (key == L"version_label") g_locale.version_label = value;
            else if (key == L"package_id_label") g_locale.package_id_label = value;
            else if (key == L"source_label") g_locale.source_label = value;
            else if (key == L"status_label") g_locale.status_label = value;
            else if (key == L"description_label") g_locale.description_label = value;
            else if (key == L"technical_info_label") g_locale.technical_info_label = value;
            else if (key == L"homepage_label") g_locale.homepage_label = value;
            else if (key == L"license_label") g_locale.license_label = value;
            else if (key == L"installer_type_label") g_locale.installer_type_label = value;
            else if (key == L"architecture_label") g_locale.architecture_label = value;
            else if (key == L"tags_label") g_locale.tags_label = value;
            else if (key == L"not_installed") g_locale.not_installed = value;
            else if (key == L"installed") g_locale.installed = value;
            else if (key == L"no_description") g_locale.no_description = value;
            else if (key == L"unknown") g_locale.unknown = value;
            else if (key == L"not_available") g_locale.not_available = value;
            else if (key == L"no_tags") g_locale.no_tags = value;
            else if (key == L"about_title") g_locale.about_title = value;
            else if (key == L"about_subtitle") g_locale.about_subtitle = value;
            else if (key == L"about_published") g_locale.about_published = value;
            else if (key == L"about_version") g_locale.about_version = value;
            else if (key == L"about_suite_desc") g_locale.about_suite_desc = value;
            else if (key == L"about_author") g_locale.about_author = value;
            else if (key == L"about_copyright") g_locale.about_copyright = value;
            else if (key == L"about_wpm_title") g_locale.about_wpm_title = value;
            else if (key == L"about_wpm_usage") g_locale.about_wpm_usage = value;
            else if (key == L"about_license_info") g_locale.about_license_info = value;
            else if (key == L"about_github") g_locale.about_github = value;
            else if (key == L"about_view_license") g_locale.about_view_license = value;
            else if (key == L"about_close") g_locale.about_close = value;
            // Error messages
            else if (key == L"error_title") g_locale.error_title = value;
            else if (key == L"window_registration_failed") g_locale.window_registration_failed = value;
            else if (key == L"window_creation_failed") g_locale.window_creation_failed = value;
            else if (key == L"database_open_failed") g_locale.database_open_failed = value;
            else if (key == L"database_error_title") g_locale.database_error_title = value;
            else if (key == L"discovery_failed_title") g_locale.discovery_failed_title = value;
            else if (key == L"discovery_failed_msg") g_locale.discovery_failed_msg = value;
            else if (key == L"about_window_error") g_locale.about_window_error = value;
            else if (key == L"license_window_error") g_locale.license_window_error = value;
            else if (key == L"wait_for_update") g_locale.wait_for_update = value;
            else if (key == L"cancel_installation") g_locale.cancel_installation = value;
            else if (key == L"preparing_installation") g_locale.preparing_installation = value;
        }
    }

    g_currentLang = lang;
    return true;
}

// Format number with thousands separator
std::wstring FormatNumber(int num) {
    std::wstring str = std::to_wstring(num);
    std::wstring result;
    int count = 0;

    // Get thousands separator (default to comma if not set)
    std::wstring sep = g_locale.thousands_sep.empty() ? L"," : g_locale.thousands_sep;

    // Add separator from right to left
    for (int i = str.length() - 1; i >= 0; i--) {
        if (count == 3) {
            result = sep + result;
            count = 0;
        }
        result = str[i] + result;
        count++;
    }

    return result;
}

// Language change handler
void OnLanguageChanged() {
    // Get selected language
    int sel = SendMessageW(g_hLangCombo, CB_GETCURSEL, 0, 0);
    std::wstring newLang;

    if (sel == 0) newLang = L"en_GB";
    else if (sel == 1) newLang = L"nb_NO";
    else if (sel == 2) newLang = L"sv_SE";

    // Load new locale
    if (LoadLocale(newLang)) {
        // Save language preference
        SaveConfig();
        
        // Update window title
        SetWindowTextW(GetParent(g_hLangCombo), g_locale.title.c_str());

        // Update button texts explicitly for owner-drawn buttons
        if (g_hSearchBtn) SetWindowTextW(g_hSearchBtn, g_locale.search_btn.c_str());
        if (g_hEndSearchBtn) SetWindowTextW(g_hEndSearchBtn, g_locale.end_search_btn.c_str());
        if (g_hInstalledBtn) SetWindowTextW(g_hInstalledBtn, g_locale.installed_btn.c_str());
        if (g_hQuitBtn) SetWindowTextW(g_hQuitBtn, g_locale.quit_btn.c_str());

        // Redraw buttons to update text
        if (g_hSearchBtn) InvalidateRect(g_hSearchBtn, NULL, TRUE);
        if (g_hEndSearchBtn) InvalidateRect(g_hEndSearchBtn, NULL, TRUE);
        if (g_hInstalledBtn) InvalidateRect(g_hInstalledBtn, NULL, TRUE);
        if (g_hRefreshInstalledBtn) InvalidateRect(g_hRefreshInstalledBtn, NULL, TRUE);
        if (g_hAboutBtn) InvalidateRect(g_hAboutBtn, NULL, TRUE);
        if (g_hQuitBtn) InvalidateRect(g_hQuitBtn, NULL, TRUE);

        // Reload tags and apps to update counts and "All" label
        LoadTags();
        HTREEITEM hItem = TreeView_GetSelection(g_hTagTree);
        if (hItem) {
            OnTagSelectionChanged();
        }
    }
}