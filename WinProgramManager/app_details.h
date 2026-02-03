#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Forward declaration
struct sqlite3;

// App details data structure
struct AppDetailsData {
    std::wstring package_id;
    std::wstring name;
    std::wstring version;
    std::wstring publisher;
    std::wstring source;
    std::wstring description;
    std::wstring homepage;
    std::wstring license;
    std::wstring installer_type;
    std::wstring architecture;
    std::vector<std::wstring> tags;
    bool is_installed;
    std::wstring installed_version;
    std::vector<unsigned char> icon_data;
    std::wstring icon_type;
};

// Function declarations
INT_PTR CALLBACK AppDetailsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
bool LoadAppDetails(sqlite3* db, const std::wstring& packageId, AppDetailsData& data);
void ShowAppDetailsDialog(HWND hParent, sqlite3* db, const std::wstring& packageId);
