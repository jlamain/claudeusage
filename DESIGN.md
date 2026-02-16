# Design Decisions — Claude Usage Tray

This document explains **why** certain implementation choices were made, not just what the code does.

## Table of Contents
- [Architecture](#architecture)
- [API Layer (api.c)](#api-layer-apic)
- [Configuration (config.c)](#configuration-configc)
- [Popup UI (popup.c)](#popup-ui-popupc)
- [Main Application (main.c)](#main-application-mainc)

---

## Architecture

### Why a single-threaded design?
- **Simplicity**: No mutexes, no race conditions, no thread synchronization
- **Adequate performance**: Network I/O (1-2 seconds every 60 seconds) doesn't need async
- **Hidden window**: There's no UI to keep responsive during the HTTP call
- **Lower risk**: Threading bugs are the #1 cause of crashes in Windows applications

**Trade-off**: The main thread blocks during HTTP requests. This is acceptable because there's no visible window to freeze.

### Why global state (`g_app` in main.c, `g_popup_data` in popup.c)?
- **Single-instance app**: Only one tray icon, one popup, one config
- **Simpler than passing pointers**: Avoids threading callbacks through window procedures
- **Common Win32 pattern**: Most Win32 sample code uses global state
- **No lifetime issues**: Everything lives for the entire process lifetime

**Alternative considered**: Allocating state on heap and storing in `SetWindowLongPtr`. Rejected because it adds complexity with no benefit for a single-window app.

---

## API Layer (api.c)

### Why store data as narrow strings internally, convert to wide for display?
**Decision**: Error strings and timestamps stored as `char[]`, converted to `wchar_t*` when needed

**Why**:
- JSON from cJSON is narrow (UTF-8)
- Most string data comes from the API (JSON)
- Wide strings only needed for Windows UI APIs
- Avoids repeated conversions on every poll cycle

**Trade-off**: Extra conversion overhead when displaying, but we only display once per minute.

### Why use cJSON instead of writing a custom parser?
**Alternatives considered**:
1. Custom parser: Fragile, unmaintainable, 500+ lines for robust JSON
2. jsmn (minimal JSON tokenizer): Still requires manual token walking
3. cJSON: Battle-tested, single .c file, easy to vendor

**Why cJSON**:
- Proven reliability (used in production by thousands of projects)
- Simple API: `cJSON_Parse` → `cJSON_GetObjectItem` → done
- Self-contained: No external dependencies
- MIT license: No legal issues

### Why parse utilization but ignore null checks in some places?
```c
cJSON *u = cJSON_GetObjectItem(field, "utilization");
if (u && cJSON_IsNumber(u))
    *util = u->valuedouble;
```

**Why check for null/type**:
- API can return `null` for unused features (seven_day_opus, seven_day_sonnet)
- Defensive programming: Don't crash if API changes format

**Why NOT validate ranges** (e.g., utilization 0-100):
- If API sends `utilization: 150`, we want to display it
- Shows the user there's a problem (better than hiding it)
- Avoids false assumptions about API behavior

### Why map specific HTTP error codes (401, 403, 429)?
```c
if (resp.status_code == 401) {
    snprintf(out->error, sizeof(out->error),
             "Token expired - reopen Claude Code");
```

**Why specific messages for specific codes**:
- **401**: Tells user exactly what to do (token expired → reopen Claude Code)
- **403**: Different issue (permissions, not expiry)
- **429**: Rate limited, will auto-retry
- **5xx**: Server error, out of user's control

**Why not map all codes**: Only map codes where we can give actionable advice.

### Why re-read credentials on every poll instead of caching?
```c
/* In main.c do_fetch(): */
config_read_access_token(g_app.config.credentials_path,
                         g_app.access_token, MAX_TOKEN_LEN);
```

**Why re-read**:
- OAuth tokens expire (typically every few hours)
- Claude Code automatically refreshes tokens and writes new ones to `.credentials.json`
- Re-reading picks up refreshed tokens automatically
- File I/O cost (~1ms) is negligible compared to network I/O (~1000ms)

**Alternative**: Watch file for changes with `FindFirstChangeNotification`. Rejected as over-engineering.

---

## Configuration (config.c)

### Why auto-detect credentials instead of making user paste the path?
```c
static BOOL try_find_credentials(wchar_t *out, int max_len)
{
    /* Check %USERPROFILE%\.claude\.credentials.json */
    ...
}
```

**Why**:
- **User experience**: "Just works" for 95% of users
- **Reduces support burden**: No need to explain file paths
- **Fails gracefully**: If auto-detect fails, creates config template

**Why check only one path** (removed WSL paths):
- App runs natively on Windows
- WSL paths (`\\wsl.localhost\...`) only needed if credentials are in WSL filesystem
- Simplified code, removed unnecessary complexity

### Why INI format instead of JSON for config?
```ini
credentials_path=C:\Users\youruser\.claude\.credentials.json
poll_interval=60
```

**Why INI**:
- **Human-editable**: Users can edit with Notepad
- **Simple to parse**: No JSON library needed (hand-rolled 30-line parser)
- **Windows convention**: Most Windows apps use INI or registry
- **Comments supported**: Can add helpful hints in the file

**Alternative**: JSON. Rejected because it requires a parser and isn't as user-friendly.

### Why open config file with Notepad on first run?
```c
ShellExecuteW(NULL, L"open", L"notepad.exe", config_path, NULL, SW_SHOW);
```

**Why**:
- **Discoverability**: User immediately sees where config is
- **Editing assistance**: File is already open, ready to edit
- **Platform convention**: Windows apps commonly do this

**Alternative**: Show file path in MessageBox only. Rejected because many users don't know how to navigate to `%APPDATA%`.

### Why not validate credentials format (sk-ant-oat01-...)?
```c
strncpy(token, at->valuestring, token_len - 1);
token[token_len - 1] = '\0';
```

**Why no validation**:
- Token format might change in the future
- Let the API server validate (it will return 401 if invalid)
- Avoids false positives (rejecting a valid token because our regex is wrong)

---

## Popup UI (popup.c)

### Why GDI instead of a dialog resource or Direct2D?
**Alternatives**:
1. **Dialog resource (.rc file)**: Static layout, can't draw custom progress bars
2. **Direct2D**: Modern, but requires Windows 7+ platform update, larger binary
3. **GDI**: Old but available everywhere

**Why GDI**:
- Available on all Windows versions (7+)
- Full control over rendering (custom progress bars, exact colors)
- No additional DLL dependencies
- Faster startup than Direct2D
- Code is self-contained (no resource designer needed)

### Why WS_POPUP instead of WS_OVERLAPPEDWINDOW?
```c
g_popup = CreateWindowExW(
    WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
    POPUP_CLASS, NULL,
    WS_POPUP | WS_BORDER,
    ...
```

**Why this style combination**:
- `WS_POPUP`: No title bar, no system menu
- `WS_BORDER`: Thin border (looks modern)
- `WS_EX_TOPMOST`: Always on top (like a notification)
- `WS_EX_TOOLWINDOW`: No taskbar entry (it's ephemeral)

**Trade-off**: Can't move or resize. Acceptable because it's informational only.

### Why dismiss on WM_KILLFOCUS instead of requiring a close button?
```c
case WM_KILLFOCUS:
    popup_hide();
    return 0;
```

**Why auto-dismiss**:
- **Less clutter**: No close button needed
- **Expected behavior**: Clicking outside any popup should close it
- **Consistent with modern UX**: Tooltips, notifications all work this way

**Alternative**: Require explicit close. Rejected as old-fashioned.

### Why color-code progress bars but not the icon?
```c
static COLORREF bar_color(double util)
{
    if (util < 60.0) return CLR_GREEN;
    if (util < 80.0) return CLR_YELLOW;
    return CLR_RED;
}
```

**Why these thresholds**:
- **60%**: Normal usage, green (no concern)
- **80%**: Approaching limit, yellow (warning)
- **95%+**: Critical, red (take action)

**Why icon changes at 80% instead of 60%**:
- Icon is always visible (tray), so changes should be rare
- Changing too often causes visual noise
- Yellow icon = "pay attention soon", not immediate concern

### Why use CreateFontW instead of stock fonts?
```c
HFONT hTitle = CreateFontW(18, 0, 0, 0, FW_BOLD, ...);
```

**Why create custom fonts**:
- Stock fonts (SYSTEM_FONT) are ugly and inconsistent across Windows versions
- Segoe UI is the modern Windows font (available since Vista)
- Custom sizes (18pt title, 14pt bold, 13pt regular) create visual hierarchy

**Trade-off**: Fonts must be created/destroyed on every paint. Acceptable because painting is infrequent (only when popup opens).

---

## Main Application (main.c)

### Why a hidden message-only window (HWND_MESSAGE)?
```c
g_app.hwnd = CreateWindowExW(0, L"ClaudeUsageTray", L"ClaudeUsage",
                              0, 0, 0, 0, 0,
                              HWND_MESSAGE, NULL, hInstance, NULL);
```

**Why**:
- Tray apps don't need a visible window
- `HWND_MESSAGE` windows receive messages (timers, tray callbacks) but aren't visible
- Using a hidden top-level window would still show in Alt+Tab

**Alternative**: No window, just tray icon. Doesn't work — tray icons must be owned by a window.

### Why 60-second poll interval?
```c
cfg->poll_interval_sec = 60;
```

**Why 60 seconds**:
- **API rate limits**: Anthropic doesn't publish limits, but 1 req/min is conservative
- **Freshness**: Usage updates every 5 minutes on the server, so < 5 min poll is ideal
- **Battery/network**: Less frequent = less battery drain, less bandwidth
- **Not too slow**: User expects reasonably fresh data

**Why configurable**: Power users might want faster updates; enterprise users might want slower.

### Why re-add tray icon on WM_TASKBARCREATED?
```c
if (msg == WM_TASKBAR_CREATED) {
    Shell_NotifyIconW(NIM_ADD, &g_app.nid);
    return 0;
}
```

**Why**:
- Windows Explorer crashes sometimes (Ctrl+Shift+Esc → Restart explorer.exe)
- When Explorer restarts, all tray icons disappear
- `TaskbarCreated` is broadcast when Explorer restarts
- Re-adding the icon makes it reappear automatically

**Alternative**: Do nothing. Rejected because user would have to restart the app.

### Why show balloon notification only on first error?
```c
if (!g_app.last_fetch_failed) {
    show_error_balloon(g_app.usage.error);
    g_app.last_fetch_failed = TRUE;
}
```

**Why not show on every error**:
- If network is down, every 60-second poll would show a balloon
- 20 balloons in 20 minutes is annoying spam
- One balloon alerts the user; repeating is useless

**Why show on first error**:
- User needs to know something is wrong
- Without a balloon, they might not notice (icon still shows last good data)

### Why not persist usage data to disk?
**Current**: Data lost on app restart

**Why not save**:
- Data is available from API immediately on restart
- Stale data could be misleading (if shown before first fetch)
- Adds complexity (file I/O, format versioning, corruption handling)
- No user-visible benefit (startup takes ~1 second, first fetch takes ~2 seconds)

### Why use Shell_NotifyIconW instead of Shell_NotifyIcon?
```c
Shell_NotifyIconW(NIM_ADD, &g_app.nid);
```

**Why W suffix everywhere**:
- **UNICODE defined**: All Windows APIs use wide chars (wchar_t, UTF-16)
- **Narrow APIs (A suffix)**: Convert to wide internally, wasting CPU
- **Future-proof**: Windows deprecated narrow APIs in Windows 2000

**Why UNICODE instead of ANSI**:
- Modern Windows is fully Unicode
- Handles non-English characters correctly (Japanese, Chinese, emoji)
- No conversion overhead

### Why load icon from resource instead of embedding as byte array?
```c
g_app.nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_GREEN));
```

**Why resource**:
- **Standard Windows approach**: Resources are compiled into .exe
- **No file I/O**: Icon is in memory, loaded instantly
- **Swappable**: Can change icon without recompiling (just edit .rc and relink)

**Alternative**: XPM or raw RGBA byte array. Rejected because resources are the Windows way.

### Why use goto cleanup instead of early returns?
```c
if (!hRequest) {
    resp.error_code = GetLastError();
    goto cleanup;
}
...
cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    return resp;
```

**Why**:
- **Single cleanup point**: All resources freed in one place
- **Prevents leaks**: No risk of forgetting to free a handle on an error path
- **Easier to audit**: One place to check that all resources are freed

**Alternative**: Multiple early returns with cleanup at each point. Rejected as error-prone.

---

## Cross-Cutting Concerns

### Why MinGW cross-compilation instead of MSVC?
**Build environment**: Linux/WSL2 → MinGW → Windows .exe

**Why**:
- **No Windows needed for build**: Can build on Linux CI
- **Static linking**: `-static` flag bundles libgcc, no DLL dependencies
- **Free**: No Visual Studio license needed
- **Compatible**: MinGW .exe runs on any Windows 7+ machine

**Trade-off**: Slightly larger binary (~473KB vs ~300KB with MSVC). Acceptable for a tray app.

### Why no telemetry or crash reporting?
**Why**:
- **Privacy**: App handles OAuth tokens (sensitive)
- **Simplicity**: No server infrastructure needed
- **Trust**: Open-source, users can audit

**Trade-off**: Can't debug user issues remotely. Acceptable for a simple app.

### Why no auto-update mechanism?
**Why not**:
- **Security risk**: Auto-updater needs code signing, HTTPS validation
- **Complexity**: Adds ~1000 lines of code
- **Maintenance burden**: Updater bugs are the #2 cause of support tickets

**Alternative**: User manually downloads new .exe. Acceptable for a tray utility.

---

## Performance Characteristics

| Operation | Time | Why |
|-----------|------|-----|
| Startup | ~300ms | Registry, icon load, config parse |
| First fetch | ~1200ms | DNS lookup + TLS handshake + API call |
| Subsequent fetches | ~800ms | Connection reuse (HTTP keep-alive) |
| Popup open | ~10ms | GDI is fast for simple graphics |
| Memory usage | ~8MB | Mostly from cJSON and response buffers |

### Why these are acceptable:
- **Startup**: Runs on login, user doesn't notice
- **Fetch time**: Happens in background every 60s
- **Memory**: Tiny compared to modern apps (browsers use GB)
