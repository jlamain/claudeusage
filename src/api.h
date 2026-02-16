#ifndef API_H
#define API_H

#include <windows.h>

typedef struct {
    double five_hour_util;       /* e.g., 30.0 (percentage) */
    char   five_hour_resets[64]; /* ISO 8601 reset timestamp */
    double seven_day_util;       /* e.g., 6.0 */
    char   seven_day_resets[64];
    double opus_util;            /* -1 if not available */
    double sonnet_util;          /* -1 if not available */
    BOOL   extra_enabled;
    double extra_limit;          /* monthly limit in cents */
    double extra_used;           /* used credits in cents */
    char   subscription_type[32]; /* e.g., "pro", "max", "max_200" */
    BOOL   valid;
    char   error[256];
} UsageData;

/* Fetch usage data from the Anthropic OAuth endpoint.
   access_token: OAuth bearer token (sk-ant-oat01-...)
   out: populated with results on return */
void api_fetch_usage(const char *access_token, UsageData *out);

#endif
