#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "http.h"
#include "api.h"
#include "popup.h"
#include "util.h"

/* Resource IDs (must match app.rc) */
#define IDI_GREEN   1001
#define IDI_YELLOW  1002
#define IDI_RED     1003

/* Custom messages and IDs */
#define WM_TRAYICON     (WM_APP + 1)
#define IDT_POLL_TIMER  1
#define IDM_REFRESH     2001
#define IDM_OPENCONFIG  2002
#define IDM_EXIT        2003

#define TRAY_UID        100

static UINT WM_TASKBAR_CREATED;

typedef struct {
    NOTIFYICONDATAW nid;
    HWND            hwnd;
    HINSTANCE       hInstance;
    AppConfig       config;
    UsageData       usage;
    char            access_token[MAX_TOKEN_LEN];
    BOOL            last_fetch_failed;
} AppState;

static AppState g_app;

static void update_tray_icon(void)
{
    int icon_id;
    double max_util = g_app.usage.five_hour_util;
    if (g_app.usage.seven_day_util > max_util)
        max_util = g_app.usage.seven_day_util;

    if (max_util >= 95.0)
        icon_id = IDI_RED;
    else if (max_util >= 80.0)
        icon_id = IDI_YELLOW;
    else
        icon_id = IDI_GREEN;

    HICON hIcon = LoadIconW(g_app.hInstance, MAKEINTRESOURCEW(icon_id));
    if (hIcon)
        g_app.nid.hIcon = hIcon;
}

static void update_tooltip(void)
{
    if (!g_app.usage.valid) {
        wchar_t *err = util_to_wide(g_app.usage.error);
        _snwprintf(g_app.nid.szTip, 128, L"Claude: %s",
                   err ? err : L"Error");
        free(err);
    } else {
        wchar_t remaining[32] = L"";
        if (g_app.usage.five_hour_resets[0]) {
            SYSTEMTIME st;
            if (util_parse_iso8601(g_app.usage.five_hour_resets, &st))
                util_format_time_remaining(&st, remaining, 32);
        }

        if (remaining[0])
            _snwprintf(g_app.nid.szTip, 128,
                L"Claude: 5h %.0f%% | 7d %.0f%% | Resets %s",
                g_app.usage.five_hour_util,
                g_app.usage.seven_day_util,
                remaining);
        else
            _snwprintf(g_app.nid.szTip, 128,
                L"Claude: 5h %.0f%% | 7d %.0f%%",
                g_app.usage.five_hour_util,
                g_app.usage.seven_day_util);
    }

    Shell_NotifyIconW(NIM_MODIFY, &g_app.nid);
}

static void show_error_balloon(const char *error)
{
    g_app.nid.uFlags |= NIF_INFO;
    wcscpy(g_app.nid.szInfoTitle, L"Claude Usage Error");
    wchar_t *err = util_to_wide(error);
    if (err) {
        wcsncpy(g_app.nid.szInfo, err, 256);
        free(err);
    }
    g_app.nid.dwInfoFlags = NIIF_ERROR;
    Shell_NotifyIconW(NIM_MODIFY, &g_app.nid);
    g_app.nid.uFlags &= ~NIF_INFO;
}

static void do_fetch(void)
{
    /* Re-read access token (may have been refreshed by Claude Code) */
    config_read_access_token(g_app.config.credentials_path,
                             g_app.access_token, MAX_TOKEN_LEN);

    /* Also read subscription type (user might have upgraded/downgraded) */
    config_read_subscription_type(g_app.config.credentials_path,
                                  g_app.usage.subscription_type,
                                  sizeof(g_app.usage.subscription_type));

    if (g_app.access_token[0] == '\0') {
        memset(&g_app.usage, 0, sizeof(g_app.usage));
        snprintf(g_app.usage.error, sizeof(g_app.usage.error),
                 "No access token found");
        update_tooltip();
        if (!g_app.last_fetch_failed) {
            show_error_balloon(g_app.usage.error);
            g_app.last_fetch_failed = TRUE;
        }
        return;
    }

    api_fetch_usage(g_app.access_token, &g_app.usage);

    update_tray_icon();
    update_tooltip();

    if (!g_app.usage.valid) {
        if (!g_app.last_fetch_failed) {
            show_error_balloon(g_app.usage.error);
            g_app.last_fetch_failed = TRUE;
        }
    } else {
        g_app.last_fetch_failed = FALSE;
    }
}

static void show_context_menu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_REFRESH, L"Refresh Now");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_OPENCONFIG, L"Open Config");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TASKBAR_CREATED) {
        /* Explorer restarted — re-add our tray icon */
        Shell_NotifyIconW(NIM_ADD, &g_app.nid);
        return 0;
    }

    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
            popup_show(g_app.hInstance, &g_app.usage);
            break;
        case WM_RBUTTONUP:
            show_context_menu(hwnd);
            break;
        }
        return 0;

    case WM_TIMER:
        if (wParam == IDT_POLL_TIMER)
            do_fetch();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_REFRESH:
            KillTimer(hwnd, IDT_POLL_TIMER);
            do_fetch();
            SetTimer(hwnd, IDT_POLL_TIMER,
                     (UINT)(g_app.config.poll_interval_sec * 1000), NULL);
            break;
        case IDM_OPENCONFIG: {
            wchar_t config_path[MAX_PATH_LEN];
            wchar_t dir[MAX_PATH_LEN];
            config_get_dir(dir, MAX_PATH_LEN);
            _snwprintf(config_path, MAX_PATH_LEN, L"%s\\config.ini", dir);
            ShellExecuteW(NULL, L"open", L"notepad.exe",
                          config_path, NULL, SW_SHOW);
            break;
        }
        case IDM_EXIT:
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    g_app.hInstance = hInstance;

    /* Load configuration */
    if (!config_load(&g_app.config))
        return 1;

    /* Read initial access token */
    if (!config_read_access_token(g_app.config.credentials_path,
                                   g_app.access_token, MAX_TOKEN_LEN)) {
        wchar_t msg[2048];
        _snwprintf(msg, 2048,
            L"Could not read access token from credentials file.\n\n"
            L"Path tried:\n%s\n\n"
            L"Make sure Claude Code is logged in and the path is correct.",
            g_app.config.credentials_path);
        MessageBoxW(NULL, msg, L"Claude Usage", MB_OK | MB_ICONWARNING);
        return 1;
    }

    /* Initialize HTTP */
    if (!http_init()) {
        MessageBoxW(NULL, L"Failed to initialize HTTP.",
                    L"Claude Usage", MB_OK | MB_ICONERROR);
        return 1;
    }

    /* Register window classes */
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.lpszClassName = L"ClaudeUsageTray";
    RegisterClassExW(&wc);

    popup_register(hInstance);

    /* Register for taskbar re-creation notification */
    WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");

    /* Create hidden message window */
    g_app.hwnd = CreateWindowExW(0, L"ClaudeUsageTray", L"ClaudeUsage",
                                  0, 0, 0, 0, 0,
                                  HWND_MESSAGE, NULL, hInstance, NULL);
    if (!g_app.hwnd) {
        http_shutdown();
        return 1;
    }

    /* Set up tray icon */
    memset(&g_app.nid, 0, sizeof(g_app.nid));
    g_app.nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_app.nid.hWnd             = g_app.hwnd;
    g_app.nid.uID              = TRAY_UID;
    g_app.nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_app.nid.uCallbackMessage = WM_TRAYICON;
    g_app.nid.hIcon            = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_GREEN));
    wcscpy(g_app.nid.szTip, L"Claude Usage: Loading...");

    Shell_NotifyIconW(NIM_ADD, &g_app.nid);

    /* Set up poll timer */
    SetTimer(g_app.hwnd, IDT_POLL_TIMER,
             (UINT)(g_app.config.poll_interval_sec * 1000), NULL);

    /* Immediate first fetch */
    do_fetch();

    /* Message loop */
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Cleanup */
    Shell_NotifyIconW(NIM_DELETE, &g_app.nid);
    popup_hide();
    http_shutdown();

    return 0;
}
