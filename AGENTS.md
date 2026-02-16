# AGENT.md — Claude Usage Tray

> Context for AI agents working on this codebase.

## Project overview

A Windows system tray application (Win32, pure C) that displays Claude Pro/Max plan usage by polling the Anthropic OAuth usage API. Cross-compiled on Linux/WSL2 with MinGW-w64.

## Build

```bash
python3 gen_icon.py  # only needed once, generates res/*.ico
cmake -B build -DCMAKE_TOOLCHAIN_FILE=mingw64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Produces `build/claudeusage.exe` — a single static Windows executable.

## Architecture

```
main.c  ──>  config.c  (reads %APPDATA%\claudeusage\config.ini + .credentials.json)
   │
   ├──>  http.c   (WinHTTP HTTPS GET, synchronous, single session handle)
   │       │
   │       v
   ├──>  api.c    (GET /api/oauth/usage, parses JSON with cJSON)
   │
   └──>  popup.c  (WS_POPUP window, GDI-painted progress bars)
```

- **main.c**: `wWinMain` entry point. Creates hidden `HWND_MESSAGE` window, adds `Shell_NotifyIconW` tray icon, runs `WM_TIMER` at configurable interval (default 60s). Handles tray click events and context menu.
- **http.c**: Thin wrapper around WinHTTP. `http_init()` opens a persistent session. `http_get()` does synchronous HTTPS GET. All handles cleaned up via goto-cleanup pattern.
- **api.c**: Constructs OAuth headers, calls `http_get()` to `api.anthropic.com`, parses response with cJSON into `UsageData` struct. Error mapping for HTTP status codes and network failures.
- **popup.c**: Registers `ClaudeUsagePopup` window class. Paints usage data with GDI (progress bars, text, separators). Dismissed on `WM_KILLFOCUS` or Escape.
- **config.c**: Reads INI-style config. Auto-detects `.credentials.json` from standard Windows path. Parses credentials JSON to extract `claudeAiOauth.accessToken`.
- **util.c**: ISO 8601 parsing, time-remaining formatting, UTF-8/wide string conversion.

## API contract

**Endpoint**: `GET https://api.anthropic.com/api/oauth/usage`
**Headers**: `Authorization: Bearer <sk-ant-oat01-...>`, `anthropic-beta: oauth-2025-04-20`
**Response**:
```json
{
  "five_hour": {"utilization": 30.0, "resets_at": "2026-02-16T13:00:01+00:00"},
  "seven_day": {"utilization": 6.0, "resets_at": "2026-02-19T21:00:00+00:00"},
  "seven_day_opus": null | {"utilization": 0.0, "resets_at": ...},
  "seven_day_sonnet": null | {"utilization": 0.0, "resets_at": ...},
  "extra_usage": {"is_enabled": true, "monthly_limit": 4250, "used_credits": 0.0}
}
```

Credentials are read from Claude Code's `.credentials.json` at `claudeAiOauth.accessToken`. Tokens are re-read on each poll to pick up refreshes.

## Key constants

| Constant | Location | Value | Purpose |
|----------|----------|-------|---------|
| `IDI_GREEN/YELLOW/RED` | main.c | 1001-1003 | Icon resource IDs (must match app.rc) |
| `WM_TRAYICON` | main.c | `WM_APP+1` | Custom tray callback message |
| `IDT_POLL_TIMER` | main.c | 1 | Timer ID |
| `POPUP_WIDTH/HEIGHT` | popup.c | 310/280 | Popup window dimensions |

## Dependencies

- **cJSON** (vendored in `vendor/`, MIT license) — JSON parsing
- **WinHTTP** (`winhttp.dll`) — HTTPS requests
- **Shell32, User32, GDI32, ComCtl32** — Windows system libraries

## Conventions

- All Win32 calls use wide-char (`W` suffix) variants with `UNICODE`/`_UNICODE` defined
- Minimum target: Windows 7 (`_WIN32_WINNT=0x0601`)
- Global state in `static AppState g_app` (main.c) and `static UsageData g_popup_data` (popup.c) — intentional for single-purpose tray app
- Error strings are narrow `char[]` in data structs, converted to wide at display time
- HTTP is synchronous on the main thread — acceptable for a tray app with no visible window

## Common modifications

- **Add a new API field**: Update `UsageData` in `api.h`, parse it in `api.c:api_fetch_usage()`, display it in `popup.c` paint handler
- **Change icon thresholds**: Edit `update_tray_icon()` in `main.c` (currently 80%/95%)
- **Change popup layout**: Edit `PopupProc` `WM_PAINT` handler in `popup.c`, adjust `POPUP_HEIGHT` if needed
- **Add new config keys**: Add field to `AppConfig` in `config.h`, parse in `config.c:parse_config_file()`
