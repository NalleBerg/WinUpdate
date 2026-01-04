# WinUpdate — Friendly winget GUI

**WinUpdate** is a lightweight, native Windows GUI application that makes managing software updates easy through Microsoft's `winget` package manager. No more cryptic command lines — just a clean interface to keep your Windows applications up to date.

## ✨ Features

- **📋 Visual Package List** — See all available updates in a clear, sortable list view
- **✅ Batch Updates** — Select multiple packages and update them all at once
- **⏭️ Skip Updates** — Skip specific package versions you don't want to install
- **🔄 Unskip Management** — Review and re-enable previously skipped updates
- **💾 Persistent Settings** — Your preferences are saved between sessions
- **🌍 Multi-Language Support** — English (GB) and Norwegian (Bokmål) built-in
- **🎨 Clean UI** — Modern Windows interface with hyperlinks and visual feedback
- **🔐 Single UAC Prompt** — One elevation for all updates (not one per package)

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

## 🛠️ Current Status

**✅ Working:**
- Core update functionality
- Skip/Unskip management
- Multi-language support (English/Norwegian)
- Config dialog for future systray integration

**🚧 In Progress:**
- System tray functionality
- About dialog redesign
- Enhanced update display

This is a work in progress, but it's fully functional as a local application!

## 📂 File Locations

- **Settings:** `%APPDATA%\WinUpdate\wup_settings.ini`
- **Logs:** `%APPDATA%\WinUpdate\logs\wup_run_log.txt`
- **Localization:** `i18n\en_GB.txt`, `i18n\nb_NO.txt`

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
