#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Forward declaration
struct sqlite3;

// Locale structure (shared with main.cpp)
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
    std::wstring querying_winget;
    std::wstring wait_moment;
    std::wstring repopulating_table;
    std::wstring release_notes_title;
    std::wstring fetching_release_notes;
    std::wstring release_notes_unavailable;
    std::wstring release_notes_fetch_error;
    std::wstring close;
    // App Details Dialog
    std::wstring app_details_title;
    std::wstring publisher_label;
    std::wstring version_label;
    std::wstring package_id_label;
    std::wstring source_label;
    std::wstring status_label;
    std::wstring not_installed;
    std::wstring installed;
    std::wstring description_label;
    std::wstring technical_info_label;
    std::wstring homepage_label;
    std::wstring license_label;
    std::wstring installer_type_label;
    std::wstring architecture_label;
    std::wstring tags_label;
    std::wstring view_release_notes_btn;
    std::wstring no_description;
    std::wstring unknown;
    std::wstring not_available;
    std::wstring no_tags;
    std::wstring install_btn;
    std::wstring uninstall_btn;
    std::wstring reinstall_btn;
    std::wstring confirm_install_title;
    std::wstring confirm_install_msg;
    std::wstring confirm_uninstall_title;
    std::wstring confirm_uninstall_msg;
    std::wstring confirm_reinstall_title;
    std::wstring confirm_reinstall_msg;
    std::wstring cancel_btn;
    std::wstring refresh_installed_btn;
    std::wstring refresh_installed_tooltip;
    std::wstring search_btn;
    std::wstring end_search_btn;
    std::wstring installed_btn;
    // About Dialog
    std::wstring about_btn;
    std::wstring quit_btn;
    std::wstring quit_title;
    std::wstring quit_message;
    std::wstring yes_btn;
    std::wstring no_btn;
    std::wstring about_title;
    std::wstring about_subtitle;
    std::wstring about_published;
    std::wstring about_version;
    std::wstring about_suite_desc;
    std::wstring about_author;
    std::wstring about_copyright;
    std::wstring about_wpm_title;
    std::wstring about_wpm_usage;
    std::wstring about_license_info;
    std::wstring about_github;
    std::wstring about_view_license;
    std::wstring about_close;
    // Error messages
    std::wstring error_title;
    std::wstring window_registration_failed;
    std::wstring window_creation_failed;
    std::wstring database_open_failed;
    std::wstring database_error_title;
    std::wstring discovery_failed_title;
    std::wstring discovery_failed_msg;
    std::wstring about_window_error;
    std::wstring license_window_error;
    std::wstring wait_for_update;
    std::wstring cancel_installation;
    std::wstring preparing_installation;
};

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
    HICON hIcon;  // Pre-loaded icon from list view
    sqlite3* db;  // Database connection for syncing after operations
};

// Function declarations
INT_PTR CALLBACK AppDetailsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
bool LoadAppDetails(sqlite3* db, const std::wstring& packageId, AppDetailsData& data);
void ShowAppDetailsDialog(HWND hParent, sqlite3* db, const std::wstring& packageId, HICON hAppIcon = nullptr);
