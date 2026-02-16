#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse ISO 8601 timestamps from the Anthropic API.
 *
 * Why this approach:
 * - Uses sscanf instead of a full ISO 8601 parser to minimize dependencies
 * - Only parses the date/time portion, ignoring timezone suffix ("+00:00")
 *   because the API always returns UTC, so we don't need timezone conversion
 * - We parse into SYSTEMTIME instead of time_t because Windows APIs for time
 *   arithmetic (FILETIME) work best with SYSTEMTIME
 * - Intentionally does NOT validate date ranges (e.g. month 1-12) because
 *   if the API sends invalid data, we want to detect that via time math errors
 *   rather than silently failing here
 */
BOOL util_parse_iso8601(const char *iso, SYSTEMTIME *out)
{
    int y, mo, d, h, mi, s;
    memset(out, 0, sizeof(*out));

    /* Parse just the datetime part, ignore timezone suffix */
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 6)
        return FALSE;

    out->wYear   = (WORD)y;
    out->wMonth  = (WORD)mo;
    out->wDay    = (WORD)d;
    out->wHour   = (WORD)h;
    out->wMinute = (WORD)mi;
    out->wSecond = (WORD)s;
    return TRUE;
}

/* Format time remaining until a future timestamp.
 *
 * Why use FILETIME for arithmetic:
 * - FILETIME represents time as 100-nanosecond intervals since Jan 1, 1601
 * - This makes subtraction simple: just subtract two 64-bit integers
 * - Windows doesn't provide a direct SYSTEMTIME subtraction API
 * - Using ULARGE_INTEGER for the arithmetic avoids signed overflow issues
 *
 * Why this format choice:
 * - "3d 12h" / "2h 14m" / "45m" - progressively shorter units as time gets closer
 * - Omits seconds because they create visual noise in a 60-second poll cycle
 * - Shows two components maximum to keep the display compact (tooltip has limited space)
 * - Days are important to show because 7-day window resets can be days away
 */
void util_format_time_remaining(const SYSTEMTIME *reset, wchar_t *buf, int len)
{
    FILETIME ft_reset, ft_now;
    SYSTEMTIME st_now;
    ULARGE_INTEGER ui_reset, ui_now;

    /* Convert both times to FILETIME for arithmetic */
    SystemTimeToFileTime(reset, &ft_reset);
    GetSystemTime(&st_now);  /* Always use UTC to match API timestamps */
    SystemTimeToFileTime(&st_now, &ft_now);

    /* Pack into 64-bit integers for subtraction */
    ui_reset.LowPart  = ft_reset.dwLowDateTime;
    ui_reset.HighPart = ft_reset.dwHighDateTime;
    ui_now.LowPart    = ft_now.dwLowDateTime;
    ui_now.HighPart   = ft_now.dwHighDateTime;

    /* If already past reset time, show "now" instead of negative time */
    if (ui_reset.QuadPart <= ui_now.QuadPart) {
        _snwprintf(buf, len, L"now");
        return;
    }

    /* Convert 100ns intervals to seconds: divide by 10,000,000 */
    ULONGLONG diff_sec = (ui_reset.QuadPart - ui_now.QuadPart) / 10000000ULL;
    int days  = (int)(diff_sec / 86400);
    int hours = (int)((diff_sec % 86400) / 3600);
    int mins  = (int)((diff_sec % 3600) / 60);

    /* Progressive formatting: show most significant + next significant unit */
    if (days > 0)
        _snwprintf(buf, len, L"%dd %dh", days, hours);
    else if (hours > 0)
        _snwprintf(buf, len, L"%dh %dm", hours, mins);
    else
        _snwprintf(buf, len, L"%dm", mins);
}

/* Convert UTF-8 narrow string to wide string (heap-allocated).
 *
 * Why heap allocation:
 * - Caller doesn't know the required buffer size in advance
 * - Error strings from the API vary in length
 * - Simpler API: no need to pass buffers around or do two-pass sizing
 *
 * Why UTF-8 as the narrow format:
 * - JSON from the API is UTF-8
 * - cJSON operates on narrow strings
 * - Windows uses UTF-16 (wide chars) for UI
 * - So we store data as narrow UTF-8, convert to wide for display
 *
 * Why two-pass conversion:
 * - First pass (NULL buffer): gets required size
 * - Second pass: does actual conversion
 * - This is the standard Windows API pattern for variable-length conversions
 */
wchar_t *util_to_wide(const char *narrow)
{
    if (!narrow) return NULL;

    /* First pass: get required buffer size (including null terminator) */
    int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, NULL, 0);
    if (n <= 0) return NULL;

    wchar_t *w = (wchar_t *)malloc(n * sizeof(wchar_t));
    if (w) {
        /* Second pass: actual conversion */
        MultiByteToWideChar(CP_UTF8, 0, narrow, -1, w, n);
    }
    return w;
}

/* Convert wide string to UTF-8 narrow string (heap-allocated).
 *
 * Why this is rarely used:
 * - Most data flows from JSON (narrow) → display (wide)
 * - This is mainly for logging or writing config files
 * - Symmetric API with util_to_wide for consistency
 *
 * Why not check malloc failure:
 * - If malloc fails, we return NULL and caller handles it
 * - In a 473KB tray app, malloc failure means the system is critically
 *   low on memory - we'd rather fail gracefully (display nothing) than
 *   try to show an error dialog that would also fail
 */
char *util_to_narrow(const wchar_t *wide)
{
    if (!wide) return NULL;

    /* First pass: get required buffer size */
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;

    char *s = (char *)malloc(n);
    if (s) {
        /* Second pass: actual conversion */
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, s, n, NULL, NULL);
    }
    return s;
}
