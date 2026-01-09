#include "Config.h"
#include "startup_manager.h"
#include "ctrlw.h"
#include "unexclude_dialog.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Dialog control IDs
#define IDC_RADIO_MANUAL 3001
#define IDC_RADIO_STARTUP 3002
#define IDC_RADIO_SYSTRAY 3003
#define IDC_COMBO_POLLING 3004
#define IDC_LBL_STATUS 3005
#define IDC_BTN_APPLY 3006
#define IDC_BTN_OK 3007
#define IDC_BTN_CANCEL 3008
#define IDC_BTN_ADD_TO_TRAY 3009
#define IDC_BTN_MANAGE_EXCLUDED 3010

// Polling interval options (prime numbers in hours)
static const int POLLING_INTERVALS[] = {0, 2, 3, 5, 7, 11, 13, 17, 19, 23};
static const int POLLING_COUNT = 10;

// Settings structure
enum class StartupMode {
    Manual = 0,     // Only run manually
    Startup = 1,    // Scan at Windows startup
    SysTray = 2     // Add to system tray
};

struct ConfigSettings {
    StartupMode mode = StartupMode::Manual;
    int pollingInterval = 0;  // 0 = scan only at startup (for SysTray mode)
};

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

static std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::string GetSettingsPath() {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buf) + "\\WinUpdate\\wup_settings.ini";
    }
    return "wup_settings.ini";
}

// Load i18n translations
static std::unordered_map<std::string, std::wstring> LoadTranslations(const std::string &locale) {
    std::unordered_map<std::string, std::wstring> trans;
    
    // Load translations from locale file in executable directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    std::wstring i18nPathW = exeDir + L"\\locale\\" + Utf8ToWide(locale) + L".txt";
    
    std::ifstream i18nFile(i18nPathW.c_str());
    if (i18nFile) {
        std::string line;
        while (std::getline(i18nFile, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                trans[key] = Utf8ToWide(val);
            }
        }
    }
    
    return trans;
}

static std::wstring t(const std::unordered_map<std::string, std::wstring> &trans, const std::string &key) {
    auto it = trans.find(key);
    if (it != trans.end()) return it->second;
    return Utf8ToWide(key);
}

// Global flag to track if "Add to systray now" button was clicked
static bool g_addToTrayNowClicked = false;

bool WasAddToTrayNowClicked() {
    bool result = g_addToTrayNowClicked;
    g_addToTrayNowClicked = false; // Reset after reading
    return result;
}

static ConfigSettings LoadSettings() {
    ConfigSettings settings;
    std::string path = GetSettingsPath();
    std::ifstream ifs(path);
    if (!ifs) return settings;
    
    std::string line;
    bool inSysTray = false;
    while (std::getline(ifs, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        if (line == "[systemtraystatus]") {
            inSysTray = true;
            continue;
        } else if (line[0] == '[') {
            inSysTray = false;
            continue;
        }
        
        if (inSysTray) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                // Trim key and value
                size_t ks = key.find_first_not_of(" \t");
                size_t ke = key.find_last_not_of(" \t");
                if (ks != std::string::npos) key = key.substr(ks, ke - ks + 1);
                size_t vs = val.find_first_not_of(" \t");
                size_t ve = val.find_last_not_of(" \t");
                if (vs != std::string::npos) val = val.substr(vs, ve - vs + 1);
                
                if (key == "mode") {
                    int modeVal = std::stoi(val);
                    if (modeVal >= 0 && modeVal <= 2) {
                        settings.mode = static_cast<StartupMode>(modeVal);
                    }
                } else if (key == "polling_interval") {
                    settings.pollingInterval = std::stoi(val);
                }
            }
        }
    }
    return settings;
}

static void SaveSettings(const ConfigSettings &settings) {
    std::string path = GetSettingsPath();
    
    // Read existing file to preserve other sections
    std::stringstream content;
    std::ifstream ifs(path);
    std::string line;
    bool inSysTray = false;
    bool sysTrayWritten = false;
    
    if (ifs) {
        while (std::getline(ifs, line)) {
            if (line.find("[systemtraystatus]") != std::string::npos) {
                inSysTray = true;
                sysTrayWritten = true;
                content << "[systemtraystatus]\n";
                content << "mode=" << static_cast<int>(settings.mode) << "\n";
                content << "polling_interval=" << settings.pollingInterval << "\n";
                continue;
            } else if (!line.empty() && line[0] == '[') {
                inSysTray = false;
            }
            
            if (!inSysTray) {
                content << line << "\n";
            }
        }
        ifs.close();
    }
    
    // If [systemtraystatus] section didn't exist, add it
    if (!sysTrayWritten) {
        content << "\n[systemtraystatus]\n";
        content << "mode=" << static_cast<int>(settings.mode) << "\n";
        content << "polling_interval=" << settings.pollingInterval << "\n";
    }
    
    // Write back
    std::ofstream ofs(path);
    if (ofs) {
        ofs << content.str();
    }
}

void LoadExcludeSettings(std::unordered_map<std::string, std::string> &excludedApps) {
    excludedApps.clear();
    std::string path = GetSettingsPath();
    std::ifstream ifs(path);
    if (!ifs) return;
    
    std::string line;
    bool inExcluded = false;
    while (std::getline(ifs, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        if (line == "[excluded]") {
            inExcluded = true;
            continue;
        } else if (line[0] == '[') {
            inExcluded = false;
            continue;
        }
        
        if (inExcluded) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string packageId = line.substr(0, eq);
                std::string reason = line.substr(eq + 1);
                // Trim key and value
                size_t ks = packageId.find_first_not_of(" \t");
                size_t ke = packageId.find_last_not_of(" \t");
                if (ks != std::string::npos) packageId = packageId.substr(ks, ke - ks + 1);
                size_t vs = reason.find_first_not_of(" \t");
                size_t ve = reason.find_last_not_of(" \t");
                if (vs != std::string::npos) reason = reason.substr(vs, ve - vs + 1);
                
                if (!packageId.empty() && !reason.empty()) {
                    excludedApps[packageId] = reason;
                }
            }
        }
    }
}

void SaveExcludeSettings(const std::unordered_map<std::string, std::string> &excludedApps) {
    std::string path = GetSettingsPath();
    
    // Read existing file to preserve other sections
    std::stringstream content;
    std::ifstream ifs(path);
    std::string line;
    bool inExcluded = false;
    bool excludedWritten = false;
    
    if (ifs) {
        while (std::getline(ifs, line)) {
            if (line.find("[excluded]") != std::string::npos) {
                inExcluded = true;
                excludedWritten = true;
                content << "[excluded]\n";
                for (const auto &pair : excludedApps) {
                    content << pair.first << "=" << pair.second << "\n";
                }
                continue;
            } else if (!line.empty() && line[0] == '[') {
                inExcluded = false;
            }
            
            if (!inExcluded) {
                content << line << "\n";
            }
        }
        ifs.close();
    }
    
    // If [excluded] section didn't exist, add it
    if (!excludedWritten) {
        content << "\n[excluded]\n";
        for (const auto &pair : excludedApps) {
            content << pair.first << "=" << pair.second << "\n";
        }
    }
    
    // Write back
    std::ofstream ofs(path);
    if (ofs) {
        ofs << content.str();
    }
}

static void UpdateStatusLabel(HWND hDlg, HWND hStatus, const ConfigSettings &settings, const std::unordered_map<std::string, std::wstring> &trans) {
    std::wstring status;
    if (settings.mode == StartupMode::Manual) {
        status = t(trans, "config_status_manual");
    } else if (settings.mode == StartupMode::Startup) {
        status = t(trans, "config_status_startup_only");
    } else if (settings.mode == StartupMode::SysTray) {
        if (settings.pollingInterval == 0) {
            status = t(trans, "config_status_systray_startup");
        } else {
            wchar_t buf[256];
            swprintf(buf, 256, t(trans, "config_status_systray_polling").c_str(), settings.pollingInterval);
            status = buf;
        }
    }
    SetWindowTextW(hStatus, status.c_str());
}

bool ShowConfigDialog(HWND parent, const std::string &currentLocale) {
    // Load translations
    auto trans = LoadTranslations(currentLocale);
    
    // Load current settings
    ConfigSettings settings = LoadSettings();
    ConfigSettings originalSettings = settings;
    
    // Ensure .ini file has [systemtraystatus] section - create if missing
    std::string settingsPath = GetSettingsPath();
    std::ifstream testIfs(settingsPath);
    bool hasSysTraySection = false;
    if (testIfs) {
        std::string line;
        while (std::getline(testIfs, line)) {
            if (line.find("[systemtraystatus]") != std::string::npos) {
                hasSysTraySection = true;
                break;
            }
        }
        testIfs.close();
    }
    if (!hasSysTraySection) {
        // Create the section with default values
        SaveSettings(settings);
    }
    
    // Center dialog on parent
    RECT rcParent;
    GetWindowRect(parent, &rcParent);
    int parentCenterX = (rcParent.left + rcParent.right) / 2;
    int parentCenterY = (rcParent.top + rcParent.bottom) / 2;
    
    // Create dialog
    const int dlgW = 600, dlgH = 320;
    int dlgX = parentCenterX - dlgW / 2;
    int dlgY = parentCenterY - dlgH / 2;
    
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"#32770",  // Dialog class
        t(trans, "config_dialog_title").c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, dlgH,
        parent,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );
    
    if (!hDlg) return false;
    
    // Create controls
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    
    // Radio button 1: Only run manually
    HWND hRadioManual = CreateWindowExW(0, L"Button", 
        t(trans, "config_mode_manual").c_str(),
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP,
        20, 20, 420, 24,
        hDlg, (HMENU)IDC_RADIO_MANUAL, NULL, NULL);
    SendMessageW(hRadioManual, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Radio button 2: Scan at Windows startup
    HWND hRadioStartup = CreateWindowExW(0, L"Button", 
        t(trans, "config_mode_startup").c_str(),
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
        20, 50, 420, 24,
        hDlg, (HMENU)IDC_RADIO_STARTUP, NULL, NULL);
    SendMessageW(hRadioStartup, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Radio button 3: Add to system tray
    HWND hRadioSysTray = CreateWindowExW(0, L"Button", 
        t(trans, "config_mode_systray").c_str(),
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
        20, 80, 420, 24,
        hDlg, (HMENU)IDC_RADIO_SYSTRAY, NULL, NULL);
    SendMessageW(hRadioSysTray, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Set initial radio button state based on mode
    if (settings.mode == StartupMode::Manual) {
        CheckDlgButton(hDlg, IDC_RADIO_MANUAL, BST_CHECKED);
    } else if (settings.mode == StartupMode::Startup) {
        CheckDlgButton(hDlg, IDC_RADIO_STARTUP, BST_CHECKED);
    } else {
        CheckDlgButton(hDlg, IDC_RADIO_SYSTRAY, BST_CHECKED);
    }
    
    // Combo box (indented, below systray radio button)
    HWND hCombo = CreateWindowExW(0, L"ComboBox", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        40, 115, 540, 200,
        hDlg, (HMENU)IDC_COMBO_POLLING, NULL, NULL);
    SendMessageW(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Populate combo
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)t(trans, "config_polling_startup").c_str());
    for (int i = 1; i < POLLING_COUNT; i++) {
        wchar_t buf[256];
        swprintf(buf, 256, t(trans, "config_polling_hours").c_str(), POLLING_INTERVALS[i]);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    
    // Select current interval
    int selIdx = 0;
    for (int i = 0; i < POLLING_COUNT; i++) {
        if (POLLING_INTERVALS[i] == settings.pollingInterval) {
            selIdx = i;
            break;
        }
    }
    SendMessageW(hCombo, CB_SETCURSEL, selIdx, 0);
    
    // Enable/disable combo based on radio button state - only enabled for SysTray mode
    BOOL shouldEnable = (settings.mode == StartupMode::SysTray) ? TRUE : FALSE;
    EnableWindow(hCombo, shouldEnable);
    
    // Status label (centered between dropdown and buttons)
    HWND hStatus = CreateWindowExW(0, L"Static", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 155, 560, 20,
        hDlg, (HMENU)IDC_LBL_STATUS, NULL, NULL);
    SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
    UpdateStatusLabel(hDlg, hStatus, settings, trans);
    
    // Apply button
    HWND hApply = CreateWindowExW(0, L"Button", t(trans, "config_btn_use").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        dlgW - 370, dlgH - 70, 90, 32,
        hDlg, (HMENU)IDC_BTN_APPLY, NULL, NULL);
    SendMessageW(hApply, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // OK button
    HWND hOK = CreateWindowExW(0, L"Button", t(trans, "config_btn_ok").c_str(),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        dlgW - 270, dlgH - 70, 90, 32,
        hDlg, (HMENU)IDC_BTN_OK, NULL, NULL);
    SendMessageW(hOK, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Cancel button
    HWND hCancel = CreateWindowExW(0, L"Button", t(trans, "config_btn_cancel").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        dlgW - 170, dlgH - 70, 90, 32,
        hDlg, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // "Add to systray now" button (left side of button row, only visible when SysTray mode selected)
    HWND hAddToTray = CreateWindowExW(0, L"Button", t(trans, "config_btn_add_to_tray").c_str(),
        WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
        20, dlgH - 70, 200, 32,
        hDlg, (HMENU)IDC_BTN_ADD_TO_TRAY, NULL, NULL);
    SendMessageW(hAddToTray, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Show/hide button based on current mode
    ShowWindow(hAddToTray, (settings.mode == StartupMode::SysTray) ? SW_SHOW : SW_HIDE);
    
    // "Manage Excluded Apps" button (left side, above button row)
    HWND hManageExcluded = CreateWindowExW(0, L"Button", t(trans, "manage_excluded_apps").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        20, dlgH - 120, 200, 32,
        hDlg, (HMENU)IDC_BTN_MANAGE_EXCLUDED, NULL, NULL);
    SendMessageW(hManageExcluded, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Subclass dialog to handle messages
    struct DialogData {
        ConfigSettings* settings;
        HWND hCombo;
        HWND hStatus;
        HWND hAddToTray;
        const std::unordered_map<std::string, std::wstring>* trans;
        const std::string* locale;
        bool* dialogResult;
        bool* dialogDone;
    };
    
    DialogData dlgData;
    dlgData.settings = &settings;
    dlgData.hCombo = hCombo;
    dlgData.hStatus = hStatus;
    dlgData.hAddToTray = hAddToTray;
    dlgData.trans = &trans;
    dlgData.locale = &currentLocale;
    bool dialogResult = false;
    bool dialogDone = false;
    dlgData.dialogResult = &dialogResult;
    dlgData.dialogDone = &dialogDone;
    
    auto DlgProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) -> LRESULT {
        DialogData* pData = (DialogData*)data;
        
        if (msg == WM_COMMAND) {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            
            if ((id == IDC_RADIO_MANUAL || id == IDC_RADIO_STARTUP || id == IDC_RADIO_SYSTRAY) && code == BN_CLICKED) {
                // Update mode based on which radio button was clicked
                if (IsDlgButtonChecked(hwnd, IDC_RADIO_MANUAL) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::Manual;
                    EnableWindow(pData->hCombo, FALSE);
                    ShowWindow(pData->hAddToTray, SW_HIDE);
                } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_STARTUP) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::Startup;
                    EnableWindow(pData->hCombo, FALSE);
                    ShowWindow(pData->hAddToTray, SW_HIDE);
                } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_SYSTRAY) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::SysTray;
                    EnableWindow(pData->hCombo, TRUE);
                    ShowWindow(pData->hAddToTray, SW_SHOW);
                }
                return 0;
            } else if (id == IDC_COMBO_POLLING && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(pData->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < POLLING_COUNT) {
                    pData->settings->pollingInterval = POLLING_INTERVALS[sel];
                }
                return 0;
            } else if (id == IDC_BTN_APPLY) {
                // Read current radio button state
                if (IsDlgButtonChecked(hwnd, IDC_RADIO_MANUAL) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::Manual;
                } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_STARTUP) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::Startup;
                } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_SYSTRAY) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::SysTray;
                }
                int sel = (int)SendMessageW(pData->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < POLLING_COUNT) {
                    pData->settings->pollingInterval = POLLING_INTERVALS[sel];
                }
                SaveSettings(*pData->settings);
                
                // Verify and fix startup shortcut to match current mode
                VerifyStartupShortcut((int)pData->settings->mode);
                
                // Reload settings from .ini to ensure status reflects saved state
                ConfigSettings savedSettings = LoadSettings();
                UpdateStatusLabel(hwnd, pData->hStatus, savedSettings, *pData->trans);
                *pData->dialogResult = true;
                return 0;
            } else if (id == IDC_BTN_OK) {
                // Read current radio button state
                if (IsDlgButtonChecked(hwnd, IDC_RADIO_MANUAL) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::Manual;
                } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_STARTUP) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::Startup;
                } else if (IsDlgButtonChecked(hwnd, IDC_RADIO_SYSTRAY) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::SysTray;
                }
                int sel = (int)SendMessageW(pData->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < POLLING_COUNT) {
                    pData->settings->pollingInterval = POLLING_INTERVALS[sel];
                }
                SaveSettings(*pData->settings);
                
                // Verify and fix startup shortcut to match current mode
                VerifyStartupShortcut((int)pData->settings->mode);
                
                *pData->dialogResult = true;
                *pData->dialogDone = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;            } else if (id == IDC_BTN_MANAGE_EXCLUDED) {
                // Show the Manage Excluded Apps dialog
                if (ShowUnexcludeDialog(hwnd, *pData->locale)) {
                    // Apps were unexcluded, trigger a refresh in main window
                    HWND mainWnd = GetParent(hwnd);
                    if (mainWnd) {
                        PostMessageW(mainWnd, WM_APP + 1, 1, 0);
                    }
                }
                return 0;            } else if (id == IDC_BTN_ADD_TO_TRAY) {
                // "Add to systray now" button clicked
                // Read current settings and save
                if (IsDlgButtonChecked(hwnd, IDC_RADIO_SYSTRAY) == BST_CHECKED) {
                    pData->settings->mode = StartupMode::SysTray;
                }
                int sel = (int)SendMessageW(pData->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < POLLING_COUNT) {
                    pData->settings->pollingInterval = POLLING_INTERVALS[sel];
                }
                SaveSettings(*pData->settings);
                
                // Mode 2: Create startup shortcut with --systray
                CreateStartupShortcut(L"--systray", L"WinUpdate - Windows Update Manager (System Tray)");
                
                // Set flag to indicate we should add to tray now
                g_addToTrayNowClicked = true;
                
                *pData->dialogResult = true;
                *pData->dialogDone = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            } else if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
                *pData->dialogResult = false;
                *pData->dialogDone = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
        } else if (msg == WM_CLOSE) {
            *pData->dialogDone = true;
            return 0;
        }
        
        return DefSubclassProc(hwnd, msg, wp, lp);
    };
    
    SetWindowSubclass(hDlg, DlgProc, 0, (DWORD_PTR)&dlgData);
    
    SetFocus(hRadioManual);
    
    // Message loop
    MSG msg;
    while (!dialogDone && GetMessageW(&msg, NULL, 0, 0)) {
        // Handle Ctrl+W
        if (HandleCtrlW(hDlg, msg.message, msg.wParam, msg.lParam)) {
            dialogDone = true;
            dialogResult = false;
            break;
        }
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    RemoveWindowSubclass(hDlg, DlgProc, 0);
    DestroyWindow(hDlg);
    
    // Return true if settings changed
    return dialogResult && (settings.mode != originalSettings.mode || 
                            settings.pollingInterval != originalSettings.pollingInterval);
}
