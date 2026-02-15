# WinProgramSuite — Complete Windows Package Management System

**Latest Update:** 15 February 2026 | **Settings Scheduler Improvements & Universal Language Support**

**Note (13 February 2026):** A first-run scheduler is now created by `WinProgramManager` on initial startup. The app writes a small marker INI at `%APPDATA%\WinProgramManager\WinProgramManager.ini` and creates a Task Scheduler job `WinProgramUpdaterWeekly` to run `WinProgramUpdaterGUI.exe --hidden` weekly. The INI contains `Settings/Language` (default `en_GB`) and `Settings/UpdaterTaskCreated` (1 on success, 0 on failure). Delete the INI to force the first-run logic again.

**WinProgramSuite** is a comprehensive package management system for Windows, combining database-driven package metadata management with a friendly GUI for updates. Built on Microsoft's `winget` package manager with advanced categorization, search, filtering, and analysis capabilities.

## 📥 Getting Started

### Quick Start (Recommended)

**WinProgramManager** requires a pre-built database to function. Building the database from scratch takes several days of continuous winget queries, so we provide a current database for download:

**Download the Database:**
- **URL:** [https://prog.nalle.no/user/data/apps/WinProgramManager.db](https://prog.nalle.no/user/data/apps/WinProgramManager.db)
- **Size:** ~115 MB (too large for GitHub)
- **Contains:** 10,800+ packages with complete metadata, icons, tags, and categories

**Installation Steps:**
1. Download `WinProgramManager.db` from the link above
2. Place it in your WinProgramManager installation directory
3. Run `WinProgramUpdaterGUI.exe` to update the database to current packages
4. Launch `WinProgramManager.exe` - your installed apps will be automatically detected

**Note:** The GitHub repository includes the complete WinProgramManager package directory (executables, DLLs, locale files) but excludes the database due to size constraints. The application is currently in testing phase - use at your own discretion.

## 🏗️ Suite Components

### 📦 WinProgramManager (Database Management)
Browse, search, and manage 10,800+ Windows packages with complete metadata.

**Current Status:** ⚠️ Testing phase - Core features implemented, undergoing validation

### 🔄 WinUpdate (Automatic Updates)
Published and stable GUI for automatic Windows package updates.

**Current Status:** ✅ Published and production-ready

## 🚨 Recent Updates (2026-02-15)

### Update 20: Settings Scheduler Improvements & Universal Language Support (v2026.02.15.01)
- **UNIVERSAL TASK SCHEDULER PARSING:** Task scheduler output is now parsed language-independently, extracting day intervals (1-365) regardless of Windows display language (English, Norwegian, Chinese, Japanese, Russian, etc.)
- **CHECKBOX FIX:** Settings dialog checkbox now correctly reflects scheduler state by checking `intervalDays > 0` instead of multiple fallback conditions
- **INPUT VALIDATION:** Custom days field now validates integer input only, with range checking (1-365 days) and clear error messages
- **TIMEOUT PROTECTION:** `restore_missing_packages.ps1` script now has 15-second timeout for winget queries to prevent indefinite hanging
- **i18n PREPARATION:** Added `settings_days_invalid_integer` locale key (internationalization to be completed in next update)

**Technical Details:**
- Task scheduler parser now uses pattern-based number extraction with date/time filtering
- Supports custom intervals up to 365 days (annual updates)
- Validates input is whole number before accepting
- Works consistently across all Windows 10/11 language configurations

## 🚨 Recent Updates (2026-02-11)

### Update 19: Stability & Installer Fixes (v2026.02.11.01)
- **INSTALLED FILTER (FAST):** Made the Installed filter toggle instantaneous by reading installed package IDs from the local database instead of running a blocking `winget list` call. This eliminates the long delay on slow regions.
- **REINSTALL DIALOG FIX:** Fixed missing button text by adding the `close` locale key and rendering owner-drawn buttons from the supplied text variable.
- **INSTALL UI:** Added an extra blank line after the "querying winget" status message for better readability in the installer log.
- **FOCUS MANAGEMENT:** Main window now regains foreground focus when install/reinstall dialog closes.
- **DATABASE CLEANUP:** Removed stale entries from the `installed_apps` table to correct inflated installed-app counts; the app will populate installed apps on next startup or manual refresh.

### Update 18: Search Dialog Complete & Installation Status Fixed (v2026.02.09.01)
- **SEARCH DIALOG i18n**: Added 15 locale keys (search_dialog_title, package_name_label, publisher_label, source_label, category_label, tag_label, status_label, status_all, status_installed, status_not_installed, search_button, reset_button, close_button, cancel_button) in 3 languages
- **VISUAL OVERHAUL**: White background RGB(255,255,255), 11pt Segoe UI font, owner-drawn buttons with hover effects
- **TWO-COLUMN LAYOUT**: Radio buttons organized 50/50 (left 20-220, right 235-430) for better space utilization
- **DIALOG UNIT SIZING**: Standardized button sizes at 60x14 DLU (search) and 75x14 DLU (confirmation) ≈ 88-110px @ 11pt Segoe UI
- **UNIVERSAL ICONS**: Added appropriate Unicode icons to 12+ buttons (▼ Install, ⟲ Reinstall, ⨳ Uninstall, ✖ Close, 🔍 Search, ⏹ Stop, 📦 Installed, 🔄 Refresh, ℹ About, ⏻ Quit, ✓ Yes/OK)
- **CRITICAL FIX - Installation Status**: Fixed bug where installed apps showed "Install" button instead of "Reinstall"/"Uninstall". Root cause: LEFT JOIN returned empty string for installed_version, not NULL. Solution: Check i.package_id column (13) from JOIN using sqlite3_column_type != SQLITE_NULL
- **CONFIRMATION DIALOG IMPROVEMENTS**: Added IDI_QUESTION icon, white background, wider buttons (75 DLU), all actions (install/reinstall/uninstall) now have confirmation dialogs
- **SIMPLIFIED ICONS**: Replaced emojis (⬇ 🗑 🔄) with simpler Unicode characters (▼ ⨳ ⟲) for better rendering across systems
- **MISSING LOCALE STRINGS**: Added confirm_install_title and confirm_install_msg that were missing when other confirm actions were added
- **BUTTON WIDTH ADJUSTMENTS**: Expanded Installed button to 120px (was 100px), Refresh Installed to 220px (was 180px) to accommodate icons
- **CODE CONSISTENCY**: Unified WM_DRAWITEM pattern across all dialogs, consistent use of GetStockObject(WHITE_BRUSH) for white backgrounds

### Update 17: Design/Layout/Look & Feel Complete (v2026.02.08.02)
- **CUSTOM QUIT DIALOG**: Professional quit confirmation with custom-drawn question mark icon (IDI_QUESTION) replacing standard MessageBox
- **BLUE BUTTON THEME**: Quit dialog Yes/No buttons match About button color (RGB 10,57,129) for consistent visual identity
- **OPTIMAL SIZING**: Compact 260x75 dialog with 50x18 buttons and 15pt bold font for perfect readability
- **UNIFIED BUTTON IMPLEMENTATION**: App details buttons (Install/Reinstall/Uninstall/Close) now use identical GWLP_USERDATA hover tracking as About button
- **CONSISTENT COLOR SCHEME**: Install (green RGB 40,180,40), Uninstall (red RGB 180,40,40), Reinstall/Close (blue RGB 10,57,129) with proper hover states
- **FOCUS MANAGEMENT**: Description field no longer auto-selected on dialog open - focus set to Close button for better UX
- **CODE CONSISTENCY**: All dialogs now use same button subclassing pattern with per-button state tracking via GWLP_USERDATA
- **PROFESSIONAL POLISH**: System icon integration, proper mouse tracking (TME_LEAVE), and bold font usage across all dialogs
- **DESIGN MILESTONE**: Complete visual identity established - all dialogs share consistent white backgrounds, owner-drawn buttons, and professional styling

### Update 16: Professional App Details Dialog (v2026.02.08.01)
- **OWNER-DRAWN BUTTONS**: Custom-styled buttons with color themes - Install (green), Uninstall (red), Reinstall/Close (blue)
- **HOVER EFFECTS**: Button subclassing with TrackMouseEvent for lighter colors on hover
- **PURE WHITE BACKGROUND**: Consistent RGB(255,255,255) background matching spinner and main window
- **COMPLETE i18n**: All labels localized - Publisher, Version, Package ID, Source, Status, Description, Technical Information, Homepage, License, Installer Type, Architecture, Tags in 3 languages
- **VALUE LOCALIZATION**: Status values (Installed/Not Installed), placeholder texts (Unknown, Not available, No tags, No description) translated
- **CLOSE BUTTON FIX**: Blue-themed Close button with proper localized text ("Close"/"Lukk"/"Stäng")
- **IMPROVED UX**: Description field no longer auto-selected on dialog open
- **CONSISTENT STYLING**: Black text (RGB 0,0,0) on white backgrounds via WM_CTLCOLORSTATIC, WM_CTLCOLOREDIT, WM_CTLCOLORDLG
- **LOCALE EXTENSION**: 19 new locale keys (app_details_title, publisher_label, version_label, etc.) with LoadLocale parsing

### Update 15: Complete Internationalization & Quit System (v2026.02.07.08)
- **COMPLETE i18n**: 90+ locale strings covering all UI elements, error messages, and dialogs
- **QUIT SYSTEM**: Professional quit confirmation with "Yes/No" buttons for all exit methods
- **QUIT BUTTON**: Red-themed button at far left with "Quit" / "Avslutt" / "Avsluta" translations
- **CTRL+W SHORTCUT**: Windows accelerator table for Ctrl+W keyboard shortcut (works globally)
- **SMART QUIT**: Child dialogs (app details) close without confirmation, main window asks first
- **CONFIG PERSISTENCE**: Language preference saved to %APPDATA%\WinProgramManager\WinProgramManager.ini
- **UTF-8 ENCODING**: Fixed locale file reading with MultiByteToWideChar (CP_UTF8) for proper character display
- **BUTTON i18n**: Search/End Search/Installed buttons now fully translated (removed hardcoded L"" strings)
- **ERROR MESSAGES**: 15+ error/warning messages now localized in all 3 languages
- **quit_handler MODULE**: Centralized quit logic in quit_handler.cpp/.h for maintainability
- **TITLE FIX**: Removed space from "WinProgram Manager" → "WinProgramManager"
- **NORWEGIAN FIX**: "Behandler database" → "Behandler databasen" (definite form)

### Update 14: Suite Version Alignment (v2026.02.07.08)
- **UNIFIED VERSIONING**: Both WinProgramManager and WinUpdate now use synchronized version 2026.02.07.08
- **SCANNER VISIBILITY FIX**: WinUpdate scanner popup now follows main window when it gains focus
- **IMPROVED UX**: No need to minimize other apps to see scan progress - popup automatically appears on top
- **SUITE IDENTITY**: All components now share the same version number and publish date (07.02.2026)

### Update 13: About Dialog & UI Polish (v2026.02.06.09)
- **ABOUT DIALOG**: Complete About dialog with WinProgramSuite logo (wpm_logo.png), version 2026.02.06.09, published 06.02.2026
- **LICENSE VIEWER**: Full GPLv2 license display with GNU logo, syntax highlighting, and formatted text
- **ABOUT BUTTON**: Blue-themed button at far right (after language selector) with locale support: "About" / "Om" / "Om"
- **COPYLEFT**: Changed from "Copyright ©" to "Copyleft" for proper open-source terminology
- **17 NEW STRINGS**: Complete i18n for About system in English, Norwegian, and Swedish
- **ASSET PACKAGING**: GPLv2.md, GnuLogo.bmp, wpm_logo.png now included in build output
- **WINDOW BEHAVIOR**: Removed WS_EX_TOPMOST - startup spinner and dialogs allow switching to other apps
- **FOREGROUND INIT**: Startup spinner appears initially via SetForegroundWindow() then allows normal switching
- **LOCALE FIXES**: Fixed missing newlines preventing about_btn from loading correctly

### Update 11: Installed Apps Refresh & i18n Fix
- **REFRESH BUTTON**: Manual "Refresh Installed Apps" button appears when Installed filter is active
- **AUTOMATIC CLEANUP**: Registry-based verification removes apps uninstalled outside the manager
- **EXTERNAL DETECTION**: Full winget list scan discovers apps installed by other tools
- **i18n CRITICAL FIX**: Locale files now load correctly (.txt not .ini), Windows CRLF line breaks work
- **MULTI-LINE TEXT**: Startup dialog, tooltips, and manual update dialog display properly
- **NORWEGIAN READY**: All three locales (English/Norwegian/Swedish) verified working
- **VISUAL POLISH**: Bold 16pt text in manual update dialog, 60pt spinner matching startup
- **TOOLTIP SYSTEM**: WinUpdate-style multi-line tooltips with proper positioning

### Update 10: App Details Dialog UX Enhancement
- **VISUAL CLARITY**: Icon display scaled to 50x50 pixels with HALFTONE mode for optimal quality
- **READABILITY**: Font increased from 9pt to 11pt Segoe UI (~22% larger) for high-res displays
- **LAYOUT**: Compact 500x295 dialog with standardized 15px spacing between fields
- **TAGS**: 24px height with ES_MULTILINE support for horizontal scrollbar interaction
- **HOMEPAGE**: Read-only EDITTEXT control - users can select and copy URLs via right-click
- **UX POLISH**: Professional appearance with better space utilization and clarity

### Update 9: Extended Metadata & App Details Enhancement
- **29-COLUMN DATABASE**: Expanded from 21 to 29 columns with 8 new metadata fields
- **INSTALLER METADATA**: Now captures installer_type (msi, exe, nullsoft, inno, etc.) and architecture (x64, x86, arm64)
- **ENHANCED PARSING**: WinProgramUpdater extracts complete installer section from winget show
- **APP DETAILS**: Dialog displays installer type and architecture for all 10,692 packages
- **LOADING FIX**: Visible "Processing database..." text during startup with i18n support (English/Norwegian/Swedish)
- **BUILD SAFETY**: Simplified database backup - preserves data across all rebuilds

### Update 8: WinProgramUpdaterGUI Enhancements
- **LOG VIEWER**: Fixed button functionality with proper window procedure routing
- **RTF RENDERING**: Color-coded log with bold timestamps, blue "Time update took" lines, red errors, green success
- **KEYBOARD SHORTCUTS**: Ctrl+W (close), Ctrl+A (select all), Ctrl+C (copy) in both windows
- **CONTEXT MENUS**: Right-click support with Copy and Select All options
- **REUSABLE MODULE**: keyboard_shortcuts.h/.cpp for accelerator management

### Update 7: Database Protection & Build Safety
- **CRITICAL FIX**: Build scripts now UPDATE-ONLY mode - Never delete existing database
- **DATABASE PROTECTION**: Removed database deletion logic from build_everything.ps1 and build_complete_db.ps1
- **USER GUIDANCE**: Error messages direct to https://prog.nalle.no for pre-built database download (115MB)
- **UPDATER**: Step 3 now reports exact count of obsolete packages removed
- **PACKAGE SUPPORT**: Verified support for packages with + character (Microsoft.VCRedist.2015+.x86, Notepad++.Notepad++)
- **DATABASE**: Restored and verified 10,707 apps with full metadata

## 🏗️ Suite Components (Continued)

### 📦 WinProgramManager
Database builder and metadata management system for Windows packages.

**Key Features:**
- **UPDATE-ONLY MODE**: Build scripts never delete existing database - safe by design
- **Database protection**: 10,800+ app database protected from accidental purging
- Single-pass collection of all winget package data (~10,780+ packages)
- Comprehensive 20+ column database schema (SQLite)
- Icon extraction and storage as BLOB with type detection
- Category/tag system with Unicode support (Chinese, Japanese, etc.)
- Enhanced category navigation with visual folder icons
- Folder icons dynamically open/close based on selection
- Advanced search with multi-field filtering and regex support
- **Complete installed apps integration**: All installed apps in database and filter
- **Intelligent cross-referencing**: SQL-based missing package detection (instant vs 4+ hours)
- **Safe database operations**: Absolute path protection prevents accidental data loss
- **Robust winget parsing**: Handles both interactive and batch mode output formats
- **App management actions**: Re-install (winget --force) and Uninstall with DB updates
- **Refresh installed apps**: Manual winget list scan to discover external installations
- **7-step update process**: Optimized from original 8-step process
- Parses winget list and winget search output using right-to-left tokenization
- Handles regional latency (tested with Greek Windows, 60+ second delays)
- Full-text search ready (FTS5 compatible)
- Automatic tag inference from co-occurrence patterns
- Professional startup dialog with animated spinner and i18n support
- Blue selection highlighting matching Windows Explorer
- Animated loading dialogs with continuous spinner during operations
- Verbose logging with BEGIN/END markers for all operations
- Database backup system with timestamped recovery files

**User Interface:**
- Two-pane ListView interface (categories + applications)
- Custom 25×19 yellow folder icons for category navigation
- Visual feedback with open/closed folder states
- Consistent 3px spacing between icons and text
- Professional appearance with optimized margins and spacing
- Blue selection highlighting on startup and during navigation
- Brown 3D package icon as default for apps without custom icons

**Search Capabilities:**
- Multi-field search: Name, Publisher, Package ID
- Case-sensitive/insensitive and exact match options
- Regular expression support for advanced queries
- Category filtering with text search
- Refine results mode for progressive filtering
- End Search button to restore full list
- Real-time result count display

**WinProgramUpdaterGUI Features:**
- Verbose GUI mode for database updates with real-time progress
- Formatted RTF log viewer with color-coded messages
- Keyboard shortcuts (Ctrl+W/A/C) and right-click context menus
- Silent mode with --hidden flag for automated updates
- Step-by-step process display with timing information
- Bold timestamps and color-coded status messages
- Manual clipboard support for reliable text copying

**Scripts:**
- `build_everything.ps1` — Single-pass database creation with all metadata
- `add_missing_installed_packages.ps1` — Adds missing installed packages with full metadata via winget show
- `check_deleted_packages.ps1` — Safe comprehensive deletion using winget search . catalog verification
- `restore_ignored_tags_fixed.ps1` — Complete tag restoration with retry logic
- `correlate_categories.ps1` — Tag co-occurrence analysis
- `infer_categories.ps1` — Automatic category inference
- `update_metadata.ps1` — Refresh package information

**Database Schema:**
- **apps** table: Full package metadata (ID, name, version, publisher, description, homepage, license, installer type, architecture, icon, etc.)
- **categories** table: All tags/categories from winget
- **app_categories** table: Many-to-many relationships with UNIQUE constraint

### 🖥️ WinUpdate
Lightweight GUI application for managing software updates through winget.

**Key Features:**
- Visual package list with batch updates
- Skip/Unskip management for version control
- Multi-language support (English GB, Norwegian, Swedish)
- System tray mode with automatic periodic scanning
- Real-time progress tracking with download percentages
- Single UAC prompt for all updates
- Startup integration with self-healing shortcuts

## 🚀 Quick Start

### Prerequisites
- Windows 10/11
- winget (Microsoft App Installer) — [Install from Microsoft Store](https://apps.microsoft.com/detail/9NBLGGH4NNS1)
- PowerShell 5.1+ for database scripts
- CMake and MinGW GCC for building C++ applications

### Building the Database

```powershell
# Navigate to WinProgramManager
cd WinProgramManager

# Build complete database (~35 hours for full collection)
.\build_everything.ps1

# Or build with a test subset
.\build_everything.ps1 -MaxPackages 100
```

**Note:** Database files (*.db) are excluded from git due to size (113+ MB). Each user must build their own database.

### Building WinUpdate

```powershell
# Navigate to WinUpdate
cd WinUpdate

# Build the application
.\makeit.bat

# Run it
.\WinUpdate.exe
```

Or use CMake:
```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --config Release
```

## 📖 How to Use

### Database Management

1. **Initial Build** — Run `build_everything.ps1` to create the database (one-time, ~35 hours)
2. **Tag Restoration** — Run `restore_ignored_tags_fixed.ps1` to add all missing tags
3. **Correlation Analysis** — Run `correlate_categories.ps1` to find tag patterns
4. **Category Inference** — Run `infer_categories.ps1` to auto-categorize packages
5. **Updates** — Use `update_metadata.ps1` for weekly metadata refresh

### WinUpdate Application

1. **Launch WinUpdate** — App automatically scans for available updates
2. **Review Updates** — See which packages have newer versions
3. **Select Updates** — Check boxes for packages you want to update
4. **Skip Versions** (Optional) — Click "Skip" to skip specific versions
5. **Upgrade** — Click "Upgrade now" to install selected updates
6. **Configure** — Choose between Manual, Hidden Scan, or System Tray mode

#### System Tray Mode
- Icon appears in system tray with next scan time
- Right-click menu: "⚡ Scan now!", "🪟 Open main window", "❌ Exit"
- Automatic scanning every X hours (1-24 configurable)
- Balloon notifications when updates found
- Startup shortcut automatically created

## 🛠️ Architecture

### Database Layer
- **SQLite3** for relational data storage
- **UTF-8 encoding** for international character support
- **COLLATE NOCASE** for case-insensitive Unicode queries
- **Parameterized queries** to prevent SQL injection
- **Transaction-based** operations for data integrity
- **Icon storage** as BLOB with MIME type detection

### PowerShell Scripts
- **Retry logic** for winget query failures (3 attempts)
- **Resume-safe** design for long-running processes
- **Color-coded output** (Cyan/Green/Yellow/Red)
- **Milestone reporting** every 1000 packages
- **Comprehensive logging** with timestamps

### C++ Applications
- **Native Windows** Win32 API
- **Single instance** handling
- **UAC elevation** via PowerShell helper
- **Multi-language** INI-based localization
- **System tray** integration

## 📂 Project Structure

```
WinProgramSuite/
├── WinProgramManager/          # Database management
│   ├── build_everything.ps1    # Main database builder
│   ├── restore_ignored_tags_fixed.ps1  # Tag restoration
│   ├── correlate_categories.ps1        # Pattern analysis
│   ├── infer_categories.ps1            # Auto-categorization
│   ├── main.cpp                        # Database query tool (C++)
│   └── sqlite3/                        # SQLite binaries
│
├── WinUpdate/                  # GUI application
│   ├── src/                    # Source code
│   ├── locale/                 # Translations (EN/NO/SE)
│   ├── assets/                 # Icons and resources
│   └── WinUpdate.exe           # Built executable
│
├── README.md                   # This file
├── Changelog.html             # English changelog
├── Changelog_no.html          # Norwegian changelog
└── Changelog_se.html          # Swedish changelog
```

## 🎯 Current Status

### ✅ Working
- Complete database building system
- Tag restoration with retry logic and Unicode support
- Correlation analysis for tag co-occurrence
- WinUpdate GUI with all features
- Multi-language support (EN/NO/SE)
- System tray mode with automatic scanning
- Startup integration with self-healing

### 🚧 In Progress
- Category inference from correlations
- "Uncategorized" category for packages with no tags
- C++ GUI application for database browsing (startup dialog complete)
- Chocolatey package support
- Code signing for Windows Defender whitelisting

### 📋 Planned
- Full-text search implementation
- Advanced filtering and sorting
- Package comparison and history tracking
- Installation statistics and analytics
- Database synchronization across machines

## 🤝 Contributing

Contributions welcome! Feel free to:
- Report bugs via [Issues](https://github.com/NalleBerg/WinUpdate/issues)
- Submit pull requests (small, focused PRs preferred)
- Suggest new features or improvements
- Improve documentation or translations

## 📊 Database Statistics

After full collection (~35 hours):
- **Total packages:** ~10,634
- **Database size:** ~113 MB (excluded from git)
- **Unique categories:** ~700-2000+ (after tag restoration)
- **Categorization coverage:** 60-80% (with inference)
- **Average tags per package:** 1-3 tags
- **Icon coverage:** ~60-70% (where available from manifests)

## 📜 License

GNU General Public License v2.0 — See [LICENSE.md](WinUpdate/LICENSE.md)

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

## 🙏 Credits

Developed by [NalleBerg](https://github.com/NalleBerg)

## 📝 Notes

- **Database files not included:** Too large for GitHub (100 MB limit). Users must build their own database using the provided scripts.
- **PowerShell execution policy:** You may need to run `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser` to execute scripts.
- **Winget dependency:** All tools rely on Microsoft's `winget` package manager. Keep App Installer updated for best results.
- **Long build times:** Initial database creation takes ~35 hours due to winget query limits (~12-13 seconds per package). This is a one-time operation.
- **Unicode support:** Scripts dynamically switch console encoding to UTF-8 when querying package details to properly handle Chinese and other non-ASCII characters. Corrupted IDs are automatically detected and skipped.

---

**WinProgramSuite** transforms Windows package management from command-line utility to comprehensive database-driven system with rich metadata, intelligent categorization, and friendly interfaces.

## Release readiness

- **Release status:** Ready for publish — release will be performed tomorrow (11 February 2026).

- Committed 11 February 2026 — codebase prepared for publishing.
