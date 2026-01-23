#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <thread>
#include <atomic>
#include "resource.h"

// Control IDs
#define ID_TAG_SEARCH 1001
#define ID_TAG_TREE 1002
#define ID_APP_SEARCH 1003
#define ID_APP_LIST 1004
#define ID_LANG_COMBO 1005

// Window class name
const wchar_t CLASS_NAME[] = L"WinProgramManagerClass";

// Locale structure
struct Locale {
    std::wstring lang_name;
    std::wstring thousands_sep;
    std::wstring decimal_sep;
    std::wstring categories;
    std::wstring apps;
    std::wstring search_tags;
    std::wstring search_apps;
    std::wstring all;
    std::wstring title;
    std::wstring processing_database;
};

// Global variables
HWND g_hTagSearch = NULL;
HWND g_hTagTree = NULL;
HWND g_hTagCountLabel = NULL;
HWND g_hAppSearch = NULL;
HWND g_hAppList = NULL;
HWND g_hAppCountLabel = NULL;
HWND g_hLangCombo = NULL;
HWND g_hSplitter = NULL;
HFONT g_hFont = NULL;
HIMAGELIST g_hImageList = NULL;
HIMAGELIST g_hTreeImageList = NULL;
sqlite3* g_db = NULL;
Locale g_locale;
std::wstring g_currentLang = L"en_GB";

// Loading dialog globals
static HWND g_loadingDlg = NULL;
static HWND g_spinnerCtrl = NULL;
static int g_spinnerFrame = 0;
static std::thread g_loadingThread;
static std::atomic<bool> g_loadingThreadRunning(false);

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
        
        // Set transparent background for all static controls
        SetBkMode(hdcStatic, TRANSPARENT);
        
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
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"LoadingDialogClass",
        L"WinProgramManager",
        WS_POPUP | WS_VISIBLE | WS_CAPTION,
        0, 0, 500, 300,
        NULL, NULL, hInstance, NULL
    );
    
    // Center dialog
    RECT rc;
    GetWindowRect(g_loadingDlg, &rc);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
    SetWindowPos(g_loadingDlg, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    
    // Add logo
    HICON hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 128, 128, LR_DEFAULTCOLOR);
    if (hIcon) {
        HWND hLogo = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD | WS_VISIBLE | SS_ICON | SS_CENTERIMAGE,
            186, 20, 128, 128, g_loadingDlg, NULL, hInstance, NULL);
        SendMessageW(hLogo, STM_SETICON, (WPARAM)hIcon, 0);
    }
    
    // Add label with transparent background
    HWND hLabel = CreateWindowExW(0, L"STATIC", g_locale.processing_database.c_str(), 
        WS_CHILD | WS_VISIBLE | SS_CENTER, 50, 160, 400, 30, g_loadingDlg, NULL, hInstance, NULL);
    // Set black text on transparent background
    SetBkMode(GetDC(hLabel), TRANSPARENT);
    
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

// Structures
struct AppInfo {
    int id;
    std::wstring packageId;
    std::wstring name;
    std::wstring version;
    std::wstring publisher;
    std::wstring homepage;
    int iconIndex;
};

struct TagInfo {
    std::wstring name;
    int count;
};

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void ResizeControls(HWND hwnd);
bool OpenDatabase();
void CloseDatabase();
void LoadTags(const std::wstring& filter = L"");
void LoadApps(const std::wstring& tag, const std::wstring& filter = L"");
HBITMAP LoadIconFromBlob(const std::vector<unsigned char>& data, const std::wstring& type);
HICON LoadIconFromMemory(const unsigned char* data, int size);
void OnTagSelectionChanged();
void OnTagSearchChanged();
void OnAppSearchChanged();
void OnAppDoubleClick();
void OnLanguageChanged();
bool LoadLocale(const std::wstring& lang);
std::wstring FormatNumber(int num);
std::wstring Trim(const std::wstring& str);

// WinMain - Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;
    (void)nCmdShow;

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
        g_locale.title = L"WinProgram Manager";
        g_locale.processing_database = L"Processing database...";
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
        MessageBoxW(NULL, L"Window registration failed!", L"Error", MB_ICONERROR | MB_OK);
        return 0;
    }

    // Start loading dialog in its own thread
    g_loadingThread = std::thread(LoadingDialogThread, hInstance);
    
    // Wait for loading dialog to be created
    while (!g_loadingThreadRunning) {
        Sleep(10);
    }
    Sleep(100); // Give it time to show

    // Create window
    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        g_locale.title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 800,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        MessageBoxW(NULL, L"Window creation failed!", L"Error", MB_ICONERROR | MB_OK);
        return 0;
    }

    g_mainWindow = hwnd;
    // Don't show main window yet - will be shown by WM_USER after loading completes
    
    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Create font
            g_hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            // Post message to create controls and start loading - allows WM_CREATE to return immediately
            PostMessageW(hwnd, WM_USER + 2, 0, 0);
            
            return 0;
        }
        
        case WM_USER + 2: {
            // Create controls on main thread
            CreateControls(hwnd);
            
            // Now post message to start background loading
            PostMessageW(hwnd, WM_USER + 1, 0, 0);
            return 0;
        }
        
        case WM_USER + 1: {
            // Start background thread to load data
            std::thread([hwnd]() {
                // Open database
                if (!OpenDatabase()) {
                    MessageBoxW(hwnd, L"Failed to open database!", L"Error", MB_ICONERROR | MB_OK);
                    PostQuitMessage(1);
                    return;
                }
                
                // Load data
                LoadTags();
                // LoadApps is called by TreeView_SelectItem in LoadTags via OnTagSelectionChanged
                // Wait a bit for LoadApps to complete
                Sleep(100);
                
                // Signal main window to show and close loading dialog
                PostMessageW(hwnd, WM_USER, 0, 0);
            }).detach();
            
            return 0;
        }
        
        case WM_USER: {
            // Loading complete - show main window and destroy loading dialog
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
            
            // Bring window to foreground
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
            BringWindowToTop(hwnd);
            
            // Signal loading thread to stop and destroy dialog
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
            return 0;
        }

        case WM_SIZE: {
            ResizeControls(hwnd);
            return 0;
        }

        case WM_COMMAND: {
            if (HIWORD(wParam) == EN_CHANGE) {
                if ((HWND)lParam == g_hTagSearch) {
                    OnTagSearchChanged();
                } else if ((HWND)lParam == g_hAppSearch) {
                    OnAppSearchChanged();
                }
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
            
            if (nmhdr->idFrom == ID_TAG_TREE && nmhdr->code == TVN_SELCHANGED) {
                OnTagSelectionChanged();
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
            
            // Make count labels blue
            if (hStatic == g_hTagCountLabel || hStatic == g_hAppCountLabel) {
                SetTextColor(hdcStatic, RGB(0, 102, 204)); // Blue color
                SetBkMode(hdcStatic, TRANSPARENT);
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Draw splitter
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.left = g_splitterPos - 2;
            rc.right = g_splitterPos + 2;
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_DESTROY:
            CloseDatabase();
            if (g_hFont) DeleteObject(g_hFont);
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
    
    // Left panel - Tag search
    g_hTagSearch = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_TAG_SEARCH,
        hInst,
        NULL
    );
    SendMessageW(g_hTagSearch, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hTagSearch, EM_SETCUEBANNER, TRUE, (LPARAM)g_locale.search_tags.c_str());
    
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
    
    // Left panel - Tag tree
    g_hTagTree = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_TREEVIEW,
        L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_TAG_TREE,
        hInst,
        NULL
    );
    SendMessageW(g_hTagTree, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    
    // Create tree view image list with real folder icons from shell
    g_hTreeImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 2, 2);
    
    HMODULE hShell32 = LoadLibraryW(L"shell32.dll");
    if (hShell32) {
        // Extract yellow closed folder icon (index 3 in shell32.dll)
        HICON hClosedIcon = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(4), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (hClosedIcon) {
            ImageList_AddIcon(g_hTreeImageList, hClosedIcon);
            DestroyIcon(hClosedIcon);
        }
        
        // Extract yellow open folder icon (index 5 in shell32.dll)
        HICON hOpenIcon = (HICON)LoadImageW(hShell32, MAKEINTRESOURCEW(5), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (hOpenIcon) {
            ImageList_AddIcon(g_hTreeImageList, hOpenIcon);
            DestroyIcon(hOpenIcon);
        }
        
        FreeLibrary(hShell32);
    }
    
    TreeView_SetImageList(g_hTagTree, g_hTreeImageList, TVSIL_NORMAL);
    
    // Pump messages to keep spinner alive
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    // Right panel - App search
    g_hAppSearch = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        hwnd,
        (HMENU)ID_APP_SEARCH,
        hInst,
        NULL
    );
    SendMessageW(g_hAppSearch, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hAppSearch, EM_SETCUEBANNER, TRUE, (LPARAM)g_locale.search_apps.c_str());
    
    // App count label
    g_hAppCountLabel = CreateWindowExW(
        0,
        L"STATIC",
        L"0 apps",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
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
    g_hImageList = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 100, 100);
    
    // Create a blue dot icon as default (for apps without icons)
    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, 16, 16);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
    
    // Fill with transparent background
    HBRUSH hBrushBg = CreateSolidBrush(RGB(240, 240, 240));
    RECT rect = {0, 0, 16, 16};
    FillRect(hdcMem, &rect, hBrushBg);
    DeleteObject(hBrushBg);
    
    // Draw blue circle
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 120, 215)); // Windows blue
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
    HPEN hOldPen = (HPEN)SelectObject(hdcMem, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdcMem, hBrush);
    Ellipse(hdcMem, 2, 2, 14, 14); // Bigger dot (12x12)
    SelectObject(hdcMem, hOldBrush);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
    
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
    
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;
    
    col.pszText = (LPWSTR)L"Name";
    col.cx = 300;
    ListView_InsertColumn(g_hAppList, 0, &col);
    
    col.pszText = (LPWSTR)L"Version";
    col.cx = 100;
    ListView_InsertColumn(g_hAppList, 1, &col);
    
    col.pszText = (LPWSTR)L"Publisher";
    col.cx = 200;
    ListView_InsertColumn(g_hAppList, 2, &col);
}

void ResizeControls(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    const int margin = 8;
    const int searchHeight = 28;
    const int labelHeight = 20;
    const int spacing = 4;
    const int langComboWidth = 150;
    
    // Language selector (top right)
    MoveWindow(g_hLangCombo, rc.right - margin - langComboWidth, margin, langComboWidth, 200, TRUE);
    
    // Left panel
    int leftWidth = g_splitterPos - margin;
    MoveWindow(g_hTagSearch, margin, margin, leftWidth, searchHeight, TRUE);
    MoveWindow(g_hTagCountLabel, margin, margin + searchHeight + spacing, leftWidth, labelHeight, TRUE);
    MoveWindow(g_hTagTree, margin, margin + searchHeight + spacing + labelHeight + spacing, 
               leftWidth, rc.bottom - margin * 2 - searchHeight - labelHeight - spacing * 2, TRUE);
    
    // Right panel
    int rightX = g_splitterPos + 4;
    int rightWidth = rc.right - rightX - margin - langComboWidth - spacing;
    MoveWindow(g_hAppSearch, rightX, margin, rightWidth, searchHeight, TRUE);
    MoveWindow(g_hAppCountLabel, rightX, margin + searchHeight + spacing, rightWidth, labelHeight, TRUE);
    MoveWindow(g_hAppList, rightX, margin + searchHeight + spacing + labelHeight + spacing,
               rc.right - rightX - margin, rc.bottom - margin * 2 - searchHeight - labelHeight - spacing * 2, TRUE);
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
        MessageBoxW(NULL, msg.c_str(), L"Database Error", MB_ICONERROR | MB_OK);
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
        MessageBoxW(NULL, msg.c_str(), L"Database Error", MB_ICONERROR | MB_OK);
        return false;
    }
    sqlite3_finalize(stmt);
    
    return true;
}

void CloseDatabase() {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

std::wstring Trim(const std::wstring& str) {
    size_t first = str.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    size_t last = str.find_last_not_of(L" \t\r\n");
    return str.substr(first, last - first + 1);
}

// All old dialog code removed - new dialog is created in WinMain before main window

std::wstring CapitalizeFirst(const std::wstring& str) {
    if (str.empty()) return str;
    std::wstring result = str;
    result[0] = towupper(result[0]);
    return result;
}

void LoadTags(const std::wstring& filter) {
    TreeView_DeleteAllItems(g_hTagTree);
    
    if (!g_db) return;
    
    sqlite3_stmt* stmt;
    
    // Add "All" item - store as static buffer for tree view
    static wchar_t allTextBuffer[256];
    swprintf(allTextBuffer, 256, L"%s", g_locale.all.c_str());
    
    TVINSERTSTRUCTW tvi = {};
    tvi.hParent = TVI_ROOT;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tvi.item.pszText = allTextBuffer;
    tvi.item.lParam = 0; // 0 = All
    tvi.item.iImage = 0;          // Closed folder
    tvi.item.iSelectedImage = 1;  // Open folder when selected
    HTREEITEM hAll = TreeView_InsertItem(g_hTagTree, &tvi);
    TreeView_SelectItem(g_hTagTree, hAll);
    
    // Load categories - show all with 4+ apps, or 1-3 apps if any app would be orphaned without this category
    std::string sql = "SELECT c.category_name, COUNT(DISTINCT ac.app_id) as cnt "
                      "FROM categories c "
                      "LEFT JOIN app_categories ac ON c.id = ac.category_id "
                      "GROUP BY c.id, c.category_name "
                      "HAVING cnt >= 4 "
                      "   OR (cnt > 0 AND EXISTS ( "
                      "       SELECT 1 FROM app_categories ac2 "
                      "       WHERE ac2.category_id = c.id "
                      "       AND (SELECT COUNT(*) FROM app_categories ac3 WHERE ac3.app_id = ac2.app_id) = 1 "
                      "   )) "
                      "ORDER BY c.category_name;";
    
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            if (!name || !name[0]) continue; // Skip NULL or empty names
            
            int wsize = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
            if (wsize <= 1) continue; // Skip if conversion failed
            
            std::wstring tagName(wsize - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, name, -1, &tagName[0], wsize);
            
            // Trim and skip empty names
            size_t first = tagName.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos) continue;
            size_t last = tagName.find_last_not_of(L" \t\r\n");
            tagName = tagName.substr(first, last - first + 1);
            
            // Capitalize first letter
            tagName = CapitalizeFirst(tagName);
            
            // Apply filter
            if (!filter.empty()) {
                std::wstring lower_name = tagName;
                std::wstring lower_filter = filter;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::towlower);
                std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::towlower);
                
                if (lower_name.find(lower_filter) == std::wstring::npos) {
                    continue;
                }
            }
            
            // Allocate permanent string for tree view text and tag name (no count)
            wchar_t* displayText = new wchar_t[512];
            swprintf(displayText, 512, L"%s", tagName.c_str());
            std::wstring* storedTag = new std::wstring(tagName);
            
            tvi.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            tvi.item.pszText = displayText;
            tvi.item.lParam = (LPARAM)storedTag; // Store pointer to tag name
            tvi.item.iImage = 0;          // Closed folder
            tvi.item.iSelectedImage = 1;  // Open folder when selected
            TreeView_InsertItem(g_hTagTree, &tvi);
        }
        sqlite3_finalize(stmt);
    }
    
    // Update category count label
    int categoryCount = TreeView_GetCount(g_hTagTree) - 1; // -1 for "All"
    std::wstring countText = FormatNumber(categoryCount) + L" " + g_locale.categories;
    SetWindowTextW(g_hTagCountLabel, countText.c_str());
    
    TreeView_SelectItem(g_hTagTree, hAll);
}

void LoadApps(const std::wstring& tag, const std::wstring& filter) {
    ListView_DeleteAllItems(g_hAppList);
    
    if (!g_db) return;
    
    sqlite3_stmt* stmt;
    std::string sql;
    
    int appCount = 0;
    
    // First, count total apps for progress
    std::string countSql;
    if (tag == L"All") {
        countSql = "SELECT COUNT(*) FROM apps WHERE name IS NOT NULL AND TRIM(name) != ''";
        if (!filter.empty()) {
            countSql += " AND (name LIKE ?1 OR publisher LIKE ?1 OR package_id LIKE ?1)";
        }
    } else {
        int size = WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string tagUtf8(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, &tagUtf8[0], size, nullptr, nullptr);
        
        countSql = "SELECT COUNT(DISTINCT a.id) FROM apps a "
                   "JOIN app_categories ac ON a.id = ac.app_id "
                   "JOIN categories c ON ac.category_id = c.id "
                   "WHERE c.category_name = ?2 AND a.name IS NOT NULL AND TRIM(a.name) != ''";
        if (!filter.empty()) {
            countSql += " AND (a.name LIKE ?1 OR a.publisher LIKE ?1 OR a.package_id LIKE ?1)";
        }
    }
    
    sqlite3_stmt* countStmt;
    if (sqlite3_prepare_v2(g_db, countSql.c_str(), -1, &countStmt, nullptr) == SQLITE_OK) {
        if (!filter.empty()) {
            std::string filterUtf8;
            int size = WideCharToMultiByte(CP_UTF8, 0, filter.c_str(), -1, nullptr, 0, nullptr, nullptr);
            filterUtf8.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, filter.c_str(), -1, &filterUtf8[0], size, nullptr, nullptr);
            std::string filterPattern = "%" + filterUtf8 + "%";
            sqlite3_bind_text(countStmt, 1, filterPattern.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (tag != L"All") {
            int size = WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string tagUtf8(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, &tagUtf8[0], size, nullptr, nullptr);
            sqlite3_bind_text(countStmt, 2, tagUtf8.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            // Count not needed anymore
        }
        sqlite3_finalize(countStmt);
    }
    
    if (tag == L"All") {
        sql = "SELECT id, package_id, name, version, publisher, homepage, icon_data, icon_type FROM apps WHERE name IS NOT NULL AND TRIM(name) != ''";
        if (!filter.empty()) {
            sql += " AND (name LIKE ?1 OR publisher LIKE ?1 OR package_id LIKE ?1)";
        }
        sql += " ORDER BY name;";
    } else {
        // Convert tag to UTF-8
        int size = WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string tagUtf8(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, &tagUtf8[0], size, nullptr, nullptr);
        
        sql = "SELECT DISTINCT a.id, a.package_id, a.name, a.version, a.publisher, a.homepage, a.icon_data, a.icon_type "
              "FROM apps a "
              "JOIN app_categories ac ON a.id = ac.app_id "
              "JOIN categories c ON ac.category_id = c.id "
              "WHERE c.category_name = ?2 AND a.name IS NOT NULL AND TRIM(a.name) != ''";
        if (!filter.empty()) {
            sql += " AND (a.name LIKE ?1 OR a.publisher LIKE ?1 OR a.package_id LIKE ?1)";
        }
        sql += " ORDER BY a.name;";
    }
    
    if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (!filter.empty()) {
            std::string filterUtf8;
            int size = WideCharToMultiByte(CP_UTF8, 0, filter.c_str(), -1, nullptr, 0, nullptr, nullptr);
            filterUtf8.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, filter.c_str(), -1, &filterUtf8[0], size, nullptr, nullptr);
            std::string filterPattern = "%" + filterUtf8 + "%";
            sqlite3_bind_text(stmt, 1, filterPattern.c_str(), -1, SQLITE_TRANSIENT);
        }
        
        if (tag != L"All") {
            int size = WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string tagUtf8(size - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, tag.c_str(), -1, &tagUtf8[0], size, nullptr, nullptr);
            sqlite3_bind_text(stmt, 2, tagUtf8.c_str(), -1, SQLITE_TRANSIENT);
        }
        
        int index = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AppInfo* app = new AppInfo();
            app->id = sqlite3_column_int(stmt, 0);
            
            // Convert UTF-8 to wide strings with better error handling
            auto convert = [](const char* utf8) -> std::wstring {
                if (!utf8 || !utf8[0]) return L"";
                
                int wsize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, nullptr, 0);
                if (wsize == 0) {
                    // UTF-8 conversion failed, try as ANSI
                    wsize = MultiByteToWideChar(CP_ACP, 0, utf8, -1, nullptr, 0);
                    if (wsize == 0) return L"[Invalid encoding]";
                    
                    std::wstring result(wsize - 1, 0);
                    MultiByteToWideChar(CP_ACP, 0, utf8, -1, &result[0], wsize);
                    return result;
                }
                
                std::wstring result(wsize - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], wsize);
                return result;
            };
            
            app->packageId = convert((const char*)sqlite3_column_text(stmt, 1));
            app->name = convert((const char*)sqlite3_column_text(stmt, 2));
            app->version = convert((const char*)sqlite3_column_text(stmt, 3));
            app->publisher = convert((const char*)sqlite3_column_text(stmt, 4));
            app->homepage = convert((const char*)sqlite3_column_text(stmt, 5));
            app->iconIndex = -1;
            
            // Load icon from the current query result (columns 6 and 7)
            if (sqlite3_column_type(stmt, 6) == SQLITE_BLOB) {
                const void* blobData = sqlite3_column_blob(stmt, 6);
                int blobSize = sqlite3_column_bytes(stmt, 6);
                
                if (blobData && blobSize > 0) {
                    // Load icon from BLOB into image list
                    HICON hIcon = LoadIconFromMemory((const unsigned char*)blobData, blobSize);
                    if (hIcon) {
                        app->iconIndex = ImageList_AddIcon(g_hImageList, hIcon);
                        DestroyIcon(hIcon);
                    }
                }
            }
            
            // Add to list view
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE;
            lvi.iItem = index++;
            lvi.iSubItem = 0;
            // Add 3 spaces before name for spacing from icon
            std::wstring displayName = L"   " + app->name;
            lvi.pszText = (LPWSTR)displayName.c_str();
            lvi.lParam = (LPARAM)app;
            lvi.iImage = app->iconIndex >= 0 ? app->iconIndex : 0;
            ListView_InsertItem(g_hAppList, &lvi);
            
            ListView_SetItemText(g_hAppList, lvi.iItem, 1, (LPWSTR)app->version.c_str());
            ListView_SetItemText(g_hAppList, lvi.iItem, 2, (LPWSTR)app->publisher.c_str());
            
            appCount++;
        }
        sqlite3_finalize(stmt);
    }
    
    // Update app count label
    std::wstring countText = FormatNumber(appCount) + L" " + g_locale.apps;
    SetWindowTextW(g_hAppCountLabel, countText.c_str());
}

void OnTagSelectionChanged() {
    static HTREEITEM hPrevSelection = NULL;
    
    HTREEITEM hItem = TreeView_GetSelection(g_hTagTree);
    if (!hItem) return;
    
    // Close previous folder icon
    if (hPrevSelection) {
        TVITEMW prevItem = {};
        prevItem.mask = TVIF_HANDLE | TVIF_IMAGE;
        prevItem.hItem = hPrevSelection;
        prevItem.iImage = 0; // Closed folder
        TreeView_SetItem(g_hTagTree, &prevItem);
    }
    
    // Open current folder icon
    TVITEMW currItem = {};
    currItem.mask = TVIF_HANDLE | TVIF_IMAGE;
    currItem.hItem = hItem;
    currItem.iImage = 1; // Open folder
    TreeView_SetItem(g_hTagTree, &currItem);
    
    hPrevSelection = hItem;
    
    TVITEMW item = {};
    item.mask = TVIF_PARAM;
    item.hItem = hItem;
    TreeView_GetItem(g_hTagTree, &item);
    
    if (item.lParam == 0) {
        g_selectedTag = L"All";
    } else {
        g_selectedTag = *(std::wstring*)item.lParam;
    }
    
    // Get app search filter
    wchar_t searchText[256] = {};
    GetWindowTextW(g_hAppSearch, searchText, 256);
    
    LoadApps(g_selectedTag, Trim(searchText));
}

void OnTagSearchChanged() {
    wchar_t searchText[256] = {};
    GetWindowTextW(g_hTagSearch, searchText, 256);
    LoadTags(Trim(searchText));
}

void OnAppSearchChanged() {
    wchar_t searchText[256] = {};
    GetWindowTextW(g_hAppSearch, searchText, 256);
    LoadApps(g_selectedTag, Trim(searchText));
}

void OnAppDoubleClick() {
    int index = ListView_GetNextItem(g_hAppList, -1, LVNI_SELECTED);
    if (index == -1) return;
    
    LVITEMW lvi = {};
    lvi.mask = LVIF_PARAM;
    lvi.iItem = index;
    ListView_GetItem(g_hAppList, &lvi);
    
    AppInfo* app = (AppInfo*)lvi.lParam;
    if (app && !app->homepage.empty()) {
        ShellExecuteW(NULL, L"open", app->homepage.c_str(), NULL, NULL, SW_SHOWNORMAL);
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
// Load locale from file
bool LoadLocale(const std::wstring& lang) {
    // Build file path
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exePath(path);
    size_t lastSlash = exePath.find_last_of(L"\\/");
    std::wstring localeFile = exePath.substr(0, lastSlash + 1) + L"locale\\" + lang + L".txt";

    // Open file
    std::wifstream file(localeFile.c_str());
    if (!file.is_open()) {
        return false;
    }

    // Read key-value pairs
    std::wstring line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == L'#') continue;

        // Parse key=value
        size_t eq = line.find(L'=');
        if (eq != std::wstring::npos) {
            std::wstring key = line.substr(0, eq);
            std::wstring value = line.substr(eq + 1);

            // Trim whitespace
            key = Trim(key);
            value = Trim(value);

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
        }
    }

    file.close();
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
        // Update window title
        SetWindowTextW(GetParent(g_hLangCombo), g_locale.title.c_str());

        // Update cue banners
        SendMessageW(g_hTagSearch, EM_SETCUEBANNER, TRUE, (LPARAM)g_locale.search_tags.c_str());
        SendMessageW(g_hAppSearch, EM_SETCUEBANNER, TRUE, (LPARAM)g_locale.search_apps.c_str());

        // Reload tags and apps to update counts and "All" label
        LoadTags();
        HTREEITEM hItem = TreeView_GetSelection(g_hTagTree);
        if (hItem) {
            OnTagSelectionChanged();
        }
    }
}