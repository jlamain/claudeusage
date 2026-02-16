#ifndef UTIL_H
#define UTIL_H

#include <windows.h>

/* Parse an ISO 8601 timestamp like "2026-02-16T13:00:01+00:00" into SYSTEMTIME (UTC). */
BOOL util_parse_iso8601(const char *iso, SYSTEMTIME *out);

/* Format time remaining until 'reset' as e.g. "2h 14m" or "3d 12h". */
void util_format_time_remaining(const SYSTEMTIME *reset, wchar_t *buf, int len);

/* Convert narrow UTF-8 string to wide. Caller must free() the result. */
wchar_t *util_to_wide(const char *narrow);

/* Convert wide string to narrow UTF-8. Caller must free() the result. */
char *util_to_narrow(const wchar_t *wide);

#endif
