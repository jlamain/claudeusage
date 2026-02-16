#include "popup.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <commctrl.h>

/* Base dimensions at 96 DPI (100% scaling) */
#define POPUP_WIDTH_BASE  310
#define POPUP_HEIGHT_BASE 280
#define POPUP_CLASS  L"ClaudeUsagePopup"

#define CLR_BG       RGB(255, 255, 255)
#define CLR_HEADER   RGB(45, 45, 45)
#define CLR_LABEL    RGB(80, 80, 80)
#define CLR_VALUE    RGB(30, 30, 30)
#define CLR_MUTED    RGB(140, 140, 140)
#define CLR_GREEN    RGB(34, 139, 34)
#define CLR_YELLOW   RGB(200, 150, 0)
#define CLR_RED      RGB(200, 40, 40)
#define CLR_BAR_BG   RGB(230, 230, 230)
#define CLR_SEPARATOR RGB(220, 220, 220)

static HWND g_popup = NULL;
static UsageData g_popup_data;
static int g_dpi = 96;  /* Current DPI, updated on window creation */

/* Scale a dimension by current DPI.
 *
 * Why scale everything:
 * - Fonts, coordinates, window size all need scaling
 * - At 150% scaling (144 DPI), a 300px window becomes 450px
 * - Keeps UI elements proportional and readable
 */
static int scale_for_dpi(int base_value)
{
    return MulDiv(base_value, g_dpi, 96);
}

static COLORREF bar_color(double util)
{
    if (util < 0) return CLR_MUTED;
    if (util < 60.0) return CLR_GREEN;
    if (util < 80.0) return CLR_YELLOW;
    return CLR_RED;
}

/* Format subscription type for display.
 *
 * Why format here instead of storing formatted version:
 * - Raw value from API is the source of truth
 * - Formatting logic is UI concern (belongs in popup.c)
 * - Easy to update display format without touching data layer
 *
 * Subscription type formats:
 * - "pro" → "Pro"
 * - "max" → "Max"
 * - "max_200" → "Max 20x"
 * - "free" → "Free"
 * - Unknown → "Pro" (conservative default)
 */
static void format_subscription_type(const char *type, wchar_t *out, int max_len)
{
    if (!type || type[0] == '\0') {
        wcscpy(out, L"Pro");  /* Default if missing */
        return;
    }

    /* Check for "max_NNN" format (e.g., "max_200" for Max 20x) */
    if (strncmp(type, "max_", 4) == 0) {
        int multiplier = atoi(type + 4);
        if (multiplier > 0) {
            _snwprintf(out, max_len, L"Max %dx", multiplier / 10);
            return;
        }
    }

    /* Simple type names: capitalize first letter */
    if (strcmp(type, "max") == 0) {
        wcscpy(out, L"Max");
    } else if (strcmp(type, "pro") == 0) {
        wcscpy(out, L"Pro");
    } else if (strcmp(type, "free") == 0) {
        wcscpy(out, L"Free");
    } else {
        /* Unknown type: capitalize first letter of raw value */
        wchar_t *w = (wchar_t *)malloc((strlen(type) + 1) * sizeof(wchar_t));
        if (w) {
            MultiByteToWideChar(CP_UTF8, 0, type, -1, w, (int)strlen(type) + 1);
            if (w[0] >= L'a' && w[0] <= L'z')
                w[0] = w[0] - L'a' + L'A';  /* Capitalize */
            wcsncpy(out, w, max_len - 1);
            out[max_len - 1] = L'\0';
            free(w);
        } else {
            wcscpy(out, L"Pro");  /* Fallback */
        }
    }
}

static void draw_progress_bar(HDC hdc, int x, int y, int w, int h, double util)
{
    /* Background */
    RECT rc = {x, y, x + w, y + h};
    HBRUSH hBg = CreateSolidBrush(CLR_BAR_BG);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    if (util < 0) return;

    /* Filled portion */
    int fill = (int)(w * (util / 100.0));
    if (fill > w) fill = w;
    if (fill > 0) {
        RECT rcFill = {x, y, x + fill, y + h};
        HBRUSH hFill = CreateSolidBrush(bar_color(util));
        FillRect(hdc, &rcFill, hFill);
        DeleteObject(hFill);
    }
}

static void draw_separator(HDC hdc, int x, int y, int w)
{
    HPEN hPen = CreatePen(PS_SOLID, 1, CLR_SEPARATOR);
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    MoveToEx(hdc, x, y, NULL);
    LineTo(hdc, x + w, y);
    SelectObject(hdc, hOld);
    DeleteObject(hPen);
}

static void draw_usage_section(HDC hdc, HFONT hBold, HFONT hNormal,
                               int *py, const wchar_t *title,
                               double util, const char *resets_iso)
{
    int y = *py;
    int lx = scale_for_dpi(16);

    /* Title */
    SelectObject(hdc, hBold);
    SetTextColor(hdc, CLR_LABEL);
    TextOutW(hdc, lx, y, title, (int)wcslen(title));
    y += scale_for_dpi(20);

    /* Progress bar */
    draw_progress_bar(hdc, lx, y, scale_for_dpi(200), scale_for_dpi(14), util);

    /* Percentage text */
    wchar_t pct[32];
    if (util >= 0)
        _snwprintf(pct, 32, L"%.0f%%", util);
    else
        wcscpy(pct, L"N/A");
    SelectObject(hdc, hNormal);
    SetTextColor(hdc, bar_color(util));
    TextOutW(hdc, lx + scale_for_dpi(210), y, pct, (int)wcslen(pct));
    y += scale_for_dpi(20);

    /* Reset time */
    if (resets_iso[0]) {
        SYSTEMTIME st_reset;
        wchar_t remaining[64];
        if (util_parse_iso8601(resets_iso, &st_reset))
            util_format_time_remaining(&st_reset, remaining, 64);
        else
            wcscpy(remaining, L"unknown");

        wchar_t line[128];
        _snwprintf(line, 128, L"Resets in: %s", remaining);
        SetTextColor(hdc, CLR_MUTED);
        TextOutW(hdc, lx, y, line, (int)wcslen(line));
        y += scale_for_dpi(18);
    }

    y += scale_for_dpi(6);
    *py = y;
}

static LRESULT CALLBACK PopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        /* Fill background */
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        HBRUSH hBg = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rcClient, hBg);
        DeleteObject(hBg);

        SetBkMode(hdc, TRANSPARENT);

        /* Create DPI-scaled fonts */
        HFONT hTitle = CreateFontW(scale_for_dpi(18), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hBold = CreateFontW(scale_for_dpi(14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HFONT hNormal = CreateFontW(scale_for_dpi(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

        int lx = scale_for_dpi(16);  /* Left margin */
        int y = scale_for_dpi(12);
        int popup_width = scale_for_dpi(POPUP_WIDTH_BASE);

        /* Title - format with subscription type */
        wchar_t title[64];
        wchar_t sub_type[32];
        format_subscription_type(g_popup_data.subscription_type, sub_type, 32);
        _snwprintf(title, 64, L"Claude %s Usage", sub_type);
        SelectObject(hdc, hTitle);
        SetTextColor(hdc, CLR_HEADER);
        TextOutW(hdc, lx, y, title, (int)wcslen(title));
        y += scale_for_dpi(28);
        draw_separator(hdc, lx, y, popup_width - scale_for_dpi(32));
        y += scale_for_dpi(10);

        if (!g_popup_data.valid) {
            SelectObject(hdc, hNormal);
            SetTextColor(hdc, CLR_RED);
            wchar_t *err = util_to_wide(g_popup_data.error);
            if (err) {
                TextOutW(hdc, lx, y, err, (int)wcslen(err));
                free(err);
            } else {
                TextOutW(hdc, lx, y, L"Error fetching data", 19);
            }
        } else {
            /* 5-hour window */
            draw_usage_section(hdc, hBold, hNormal, &y,
                L"5-Hour Window",
                g_popup_data.five_hour_util,
                g_popup_data.five_hour_resets);

            /* 7-day window */
            draw_usage_section(hdc, hBold, hNormal, &y,
                L"7-Day Window",
                g_popup_data.seven_day_util,
                g_popup_data.seven_day_resets);

            /* Opus/Sonnet specific if available */
            if (g_popup_data.opus_util >= 0) {
                wchar_t lbl[64];
                _snwprintf(lbl, 64, L"7-Day Opus: %.0f%%", g_popup_data.opus_util);
                SelectObject(hdc, hNormal);
                SetTextColor(hdc, CLR_LABEL);
                TextOutW(hdc, lx, y, lbl, (int)wcslen(lbl));
                y += scale_for_dpi(18);
            }
            if (g_popup_data.sonnet_util >= 0) {
                wchar_t lbl[64];
                _snwprintf(lbl, 64, L"7-Day Sonnet: %.0f%%", g_popup_data.sonnet_util);
                SelectObject(hdc, hNormal);
                SetTextColor(hdc, CLR_LABEL);
                TextOutW(hdc, lx, y, lbl, (int)wcslen(lbl));
                y += scale_for_dpi(18);
            }

            /* Extra credits */
            if (g_popup_data.extra_enabled) {
                draw_separator(hdc, lx, y, popup_width - scale_for_dpi(32));
                y += scale_for_dpi(8);
                wchar_t credits[128];
                _snwprintf(credits, 128, L"Extra Credits: $%.2f / $%.2f",
                    g_popup_data.extra_used / 100.0,
                    g_popup_data.extra_limit / 100.0);
                SelectObject(hdc, hNormal);
                SetTextColor(hdc, CLR_LABEL);
                TextOutW(hdc, lx, y, credits, (int)wcslen(credits));
                y += scale_for_dpi(20);
            }
        }

        /* Timestamp */
        int popup_height = scale_for_dpi(POPUP_HEIGHT_BASE);
        SYSTEMTIME now;
        GetLocalTime(&now);
        wchar_t ts[64];
        _snwprintf(ts, 64, L"Updated: %02d:%02d:%02d",
                   now.wHour, now.wMinute, now.wSecond);
        draw_separator(hdc, lx, popup_height - scale_for_dpi(30), popup_width - scale_for_dpi(32));
        SelectObject(hdc, hNormal);
        SetTextColor(hdc, CLR_MUTED);
        TextOutW(hdc, lx, popup_height - scale_for_dpi(22), ts, (int)wcslen(ts));

        DeleteObject(hTitle);
        DeleteObject(hBold);
        DeleteObject(hNormal);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KILLFOCUS:
        popup_hide();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
            popup_hide();
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void popup_register(HINSTANCE hInstance)
{
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = PopupProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = POPUP_CLASS;
    RegisterClassExW(&wc);
}

void popup_show(HINSTANCE hInstance, const UsageData *usage)
{
    g_popup_data = *usage;

    if (g_popup) {
        InvalidateRect(g_popup, NULL, TRUE);
        SetForegroundWindow(g_popup);
        SetFocus(g_popup);
        return;
    }

    /* Get DPI for the monitor where cursor is (for multi-monitor setups).
     * We query DPI before creating the window to size it correctly. */
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    /* Get DPI for this monitor (fallback to 96 DPI if API unavailable) */
    typedef HRESULT (WINAPI *GetDpiForMonitorFunc)(HMONITOR, int, UINT*, UINT*);
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        GetDpiForMonitorFunc pGetDpiForMonitor =
            (GetDpiForMonitorFunc)GetProcAddress(hShcore, "GetDpiForMonitor");
        if (pGetDpiForMonitor) {
            UINT dpiX = 96, dpiY = 96;
            pGetDpiForMonitor(hMonitor, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY);
            g_dpi = (int)dpiX;
        }
        FreeLibrary(hShcore);
    }
    if (g_dpi == 0) g_dpi = 96;  /* Fallback */

    /* Calculate DPI-scaled dimensions */
    int popup_width = scale_for_dpi(POPUP_WIDTH_BASE);
    int popup_height = scale_for_dpi(POPUP_HEIGHT_BASE);

    /* Position above cursor (near tray area) */
    int x = pt.x - popup_width / 2;
    int y = pt.y - popup_height - scale_for_dpi(8);

    /* Clamp to screen */
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    if (x < workArea.left) x = workArea.left;
    if (x + popup_width > workArea.right) x = workArea.right - popup_width;
    if (y < workArea.top) y = workArea.top;

    g_popup = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        POPUP_CLASS, NULL,
        WS_POPUP | WS_BORDER,
        x, y, popup_width, popup_height,
        NULL, NULL, hInstance, NULL);

    ShowWindow(g_popup, SW_SHOW);
    SetForegroundWindow(g_popup);
    SetFocus(g_popup);
}

void popup_hide(void)
{
    if (g_popup) {
        DestroyWindow(g_popup);
        g_popup = NULL;
    }
}
