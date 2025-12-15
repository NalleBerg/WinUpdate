# WinUpdate — friendly winget GUI

<p align="center">
	<img src="winupdate_logo.png" alt="WinUpdate logo" width="220" />
</p>

WinUpdate is a small, lightweight Windows GUI wrapper around Microsoft's `winget` tool. It makes it easy to find and batch-update installed applications using a simple list-and-check interface.

Key features
- Simple list view of updatable packages with checkboxes and a "select all" option
- Single UAC elevation for installs (so you only confirm once)
- Prefer `--output json` where available, with resilient text parsing fallbacks
- Unicode-safe and logs raw `winget` output for debugging
- Loading dialog with animated indicator and clean visuals

Quick start
1. Build (Windows with MinGW/CMake):

```powershell
.\makeit.bat
```

2. Run the app:

```powershell
.\build\WinUpdate.exe
```

Notes
- The app does not perform installs when built in debug/test modes by default — check `src/main.cpp` for the debug flags used during development.
- If you prefer a release-ready signed installer or packaged build, I can add an automated CI step.

Contributing
- Send issues or patches; small, focused PRs are welcome.

Enjoy — and ping me when you want tweaks or a packaged release.
