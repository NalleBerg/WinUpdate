# WinUpdate — Friendly winget GUI

**Version 2026.01.11.18** | **Published:** 11 January 2026 18:00 GMT+1

**WinUpdate** is a lightweight, native Windows GUI application that makes managing software updates easy through Microsoft's `winget` package manager. No more cryptic command lines — just a clean interface to keep your Windows applications up to date.

## ✨ Features

- **📋 Visual Package List** — See all available updates in a clear, sortable list view
- **✅ Batch Updates** — Select multiple packages and update them all at once
- **⏭️ Skip Updates** — Skip specific package versions you don't want to install
- **🔄 Unskip Management** — Review and re-enable previously skipped updates
- **💾 Persistent Settings** — Your preferences are saved between sessions
- **🌍 Multi-Language Support** — English (GB), Norwegian (Bokmål), and Swedish built-in
- **🎨 Clean UI** — Modern Windows interface with hyperlinks and visual feedback
- **🔐 Single UAC Prompt** — One elevation for all updates (not one per package)
- **📦 Install Dialog** — Real-time output with progress bars, download percentages, and status tracking
- **📊 Progress Tracking** — Visual progress bar updates from 0% to 100% as packages install
- **🔔 System Tray Mode** — Run in background with automatic periodic scanning
- **🚀 Startup Integration** — Automatic shortcut management with verification and self-healing
- **⚙️ Three Configuration Modes:**
  - **Mode 0 (Manual):** Default mode - manually scan when you open the app
  - **Mode 1 (Hidden Scan):** Runs hidden scan at Windows startup, shows window if updates found
  - **Mode 2 (System Tray):** Stays in system tray, automatically scans every X hours with balloon notifications

## 🚀 Quick Start

### Prerequisites
- Windows 10/11
- winget (Microsoft App Installer) — [Install from Microsoft Store](https://apps.microsoft.com/detail/9NBLGGH4NNS1)
- CMake and MinGW GCC (for building from source)

### Building

```powershell
# Clone the repository
git clone https://github.com/NalleBerg/WinUpdate.git
cd WinUpdate

# Build the application
.\makeit.bat

# Run it
.\WinUpdate\WinUpdate.exe
```

Or use CMake directly:

```powershell
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --config Release
```

## 📖 How to Use

1. **Launch WinUpdate** — The app automatically scans for available updates
2. **Review Updates** — See which packages have newer versions available
3. **Select Updates** — Check the boxes for packages you want to update
4. **Skip Versions** (Optional) — Click the "Skip" link to skip specific versions
5. **Upgrade** — Click "Upgrade now" to install selected updates
6. **Manage Skipped** — Use the "Unskip" button to review and remove skips
7. **Configure** — Click "Config" to choose between Manual, Hidden Scan, or System Tray mode

### System Tray Mode
When Mode 2 (System Tray) is selected:
- App icon appears in system tray showing next scan time on hover
- Right-click for menu: "⚡ Scan now!", "🪟 Open main window", "❌ Exit"
- Automatically scans every X hours (configurable: 1-24 hours)
- Balloon notification when updates are found (click to open window)
- Silent automatic scans (only manual scans show "You are updated!" when no updates)
- Closing the window hides it to tray instead of exiting
- Startup shortcut automatically created with `--systray` argument

### Startup Shortcut Management
WinUpdate automatically manages Windows startup shortcuts:
- **Mode 1 (Hidden Scan):** Creates shortcut with `--hidden` argument for silent startup scan
- **Mode 2 (System Tray):** Creates shortcut with `--systray` argument to start in tray
- **Mode 0 (Manual):** Removes startup shortcut if it exists
- **Self-Healing:** Application verifies shortcut on startup and fixes if it doesn't match configuration
- Shortcuts are created in: `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`

## 🛠️ Current Status

**✅ Working:**
- Core update functionality with animated install dialog
- Skip/Unskip management
- Multi-language support (English/Norwegian/Swedish)
- System tray with automatic periodic scanning
- Three configuration modes with startup integration
- Automatic startup shortcut management with self-healing
- Single instance handling with window activation
- Select All button for batch operations
- PowerShell-based UAC elevation

**🚧 In Progress:**
- About dialog redesign
- Enhanced update display

This is a work in progress, but it's fully functional!

## 📂 File Locations

- **Settings:** `%APPDATA%\WinUpdate\wup_settings.ini`
- **Logs:** `%APPDATA%\WinUpdate\logs\wup_run_log.txt`
- **Localization:** `locale\en_GB.txt`, `locale\nb_NO.txt`, `locale\sv_SE.txt`

## 🤝 Contributing

Contributions are welcome! Feel free to:
- Report bugs via [Issues](https://github.com/NalleBerg/WinUpdate/issues)
- Submit pull requests (small, focused PRs preferred)
- Suggest new features or improvements

## 📜 License

GNU General Public License v2.0 — See [LICENSE.md](LICENSE.md) for details

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

## 🙏 Credits

Developed by [NalleBerg](https://github.com/NalleBerg)

---

**Note:** WinUpdate is a GUI wrapper and relies on Microsoft's `winget` tool. Make sure you have the latest version of App Installer for the best experience.
