# Claude Usage Tray

A lightweight Windows system tray widget that displays your Claude Pro/Max plan usage at a glance.

![Windows tray icon](https://img.shields.io/badge/platform-Windows_x64-blue) ![Language](https://img.shields.io/badge/language-C_(Win32)-orange) ![License](https://img.shields.io/badge/license-MIT-green)

## What it does

- Shows your **5-hour** and **7-day** usage percentages in the tray tooltip
- **Left-click** the tray icon for a detail popup with progress bars and reset countdowns
- **Right-click** for a context menu (Refresh / Open Config / Exit)
- Icon changes color: **green** (< 80%), **yellow** (80-95%), **red** (> 95%)
- Polls the Anthropic API every 60 seconds (configurable)
- Reads your existing Claude Code OAuth credentials automatically

## Requirements

- Windows 7 or later (x64)
- Claude Code installed and logged in (the app reads its OAuth token)

### Build requirements (if compiling yourself)

- MinGW-w64 cross-compiler (`x86_64-w64-mingw32-gcc`)
- CMake 3.20+
- Python 3 (for icon generation only)

## Quick start

### Option A: Use a pre-built release

1. Download `claudeusage.exe` from Releases
2. Run it on Windows
3. It will auto-detect your Claude Code credentials from `%USERPROFILE%\.claude\.credentials.json`
4. If auto-detection fails, it creates a config file and opens it in Notepad for you to fill in

### Option B: Build from source

```bash
# Generate tray icons
python3 gen_icon.py

# Configure and build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=mingw64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build/claudeusage.exe` (~ 473 KB, no external DLL dependencies).

## Configuration

Config file location: `%APPDATA%\claudeusage\config.ini`

```ini
# Path to your Claude Code credentials file
credentials_path=C:\Users\youruser\.claude\.credentials.json

# Poll interval in seconds (default: 60)
poll_interval=60
```

The app creates this file automatically on first run.

## How it works

1. Reads the OAuth access token from Claude Code's `.credentials.json`
2. Calls `GET https://api.anthropic.com/api/oauth/usage` with the token
3. Parses the JSON response for utilization percentages and reset times
4. Updates the tray icon tooltip and color accordingly
5. Re-reads credentials on each poll cycle, so token refreshes by Claude Code are picked up automatically

## Tray popup layout

```
Claude Pro Usage
--------------------------
5-Hour Window
[==========-------]  60%
Resets in: 2h 14m

7-Day Window
[==---------------]  12%
Resets in: 3d 8h

Extra Credits: $0.00 / $42.50
--------------------------
Updated: 14:32:05
```

## Project structure

```
claudeusage/
├── CMakeLists.txt              # Build configuration
├── mingw64-toolchain.cmake     # MinGW cross-compilation toolchain
├── gen_icon.py                 # Icon generator (pure Python, no deps)
├── src/
│   ├── main.c                  # Entry point, tray icon, message loop
│   ├── http.h/c                # WinHTTP HTTPS client
│   ├── api.h/c                 # Anthropic OAuth usage API
│   ├── popup.h/c               # Detail popup window (GDI)
│   ├── config.h/c              # Config file + credential reader
│   └── util.h/c                # Time parsing, string helpers
├── vendor/
│   └── cJSON.h/c               # JSON parser (MIT, vendored)
└── res/
    ├── app.rc                  # Resource script
    └── *.ico                   # Green/yellow/red tray icons
```

## License

MIT
