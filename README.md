# WinProgramSuite — Complete Windows Package Management System

**Latest Update:** 3 February 2026 | **App Details Dialog UX Enhancement**

**WinProgramSuite** is a comprehensive package management system for Windows, combining database-driven package metadata management with a friendly GUI for updates. Built on Microsoft's `winget` package manager with advanced categorization, search, filtering, and analysis capabilities.

## 🚨 Recent Updates (2026-02-03)

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

> **Note:** Currently only **WinUpdate** (the GUI component) is published and available. WinProgramManager (database management) is under active development.

## 🏗️ Suite Components

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
