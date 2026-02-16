#include "api.h"
#include "http.h"
#include "util.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static void parse_usage_field(cJSON *root, const char *name,
                              double *util, char *resets, int resets_len)
{
    *util = -1.0;
    resets[0] = '\0';

    cJSON *field = cJSON_GetObjectItem(root, name);
    if (!field || cJSON_IsNull(field))
        return;

    cJSON *u = cJSON_GetObjectItem(field, "utilization");
    if (u && cJSON_IsNumber(u))
        *util = u->valuedouble;

    cJSON *r = cJSON_GetObjectItem(field, "resets_at");
    if (r && cJSON_IsString(r))
        strncpy(resets, r->valuestring, resets_len - 1);
}

void api_fetch_usage(const char *access_token, UsageData *out)
{
    memset(out, 0, sizeof(*out));
    out->opus_util = -1.0;
    out->sonnet_util = -1.0;
    out->five_hour_util = -1.0;
    out->seven_day_util = -1.0;

    /* Build headers */
    wchar_t headers[1024];
    _snwprintf(headers, 1024,
        L"Authorization: Bearer %hs\r\n"
        L"anthropic-beta: oauth-2025-04-20\r\n"
        L"Accept: application/json\r\n",
        access_token);

    HttpResponse resp = http_get(L"api.anthropic.com",
                                  INTERNET_DEFAULT_HTTPS_PORT,
                                  L"/api/oauth/usage",
                                  headers);

    if (resp.error_code != 0) {
        DWORD ec = resp.error_code;
        if (ec == 12029) /* ERROR_WINHTTP_CANNOT_CONNECT */
            snprintf(out->error, sizeof(out->error),
                     "Cannot connect to api.anthropic.com");
        else if (ec == 12002) /* ERROR_WINHTTP_TIMEOUT */
            snprintf(out->error, sizeof(out->error), "Request timed out");
        else if (ec == 12007) /* ERROR_WINHTTP_NAME_NOT_RESOLVED */
            snprintf(out->error, sizeof(out->error), "DNS resolution failed");
        else
            snprintf(out->error, sizeof(out->error),
                     "Network error (code %lu)", ec);
        http_response_free(&resp);
        return;
    }

    if (resp.status_code == 401) {
        snprintf(out->error, sizeof(out->error),
                 "Token expired - reopen Claude Code");
        http_response_free(&resp);
        return;
    }
    if (resp.status_code == 403) {
        snprintf(out->error, sizeof(out->error), "Access denied");
        http_response_free(&resp);
        return;
    }
    if (resp.status_code != 200) {
        snprintf(out->error, sizeof(out->error),
                 "API error (HTTP %d)", resp.status_code);
        http_response_free(&resp);
        return;
    }

    if (!resp.body) {
        snprintf(out->error, sizeof(out->error), "Empty response");
        http_response_free(&resp);
        return;
    }

    cJSON *root = cJSON_Parse(resp.body);
    http_response_free(&resp);

    if (!root) {
        snprintf(out->error, sizeof(out->error), "JSON parse error");
        return;
    }

    parse_usage_field(root, "five_hour",
                      &out->five_hour_util, out->five_hour_resets, 64);
    parse_usage_field(root, "seven_day",
                      &out->seven_day_util, out->seven_day_resets, 64);

    /* Opus and Sonnet specific limits */
    cJSON *opus = cJSON_GetObjectItem(root, "seven_day_opus");
    if (opus && !cJSON_IsNull(opus)) {
        cJSON *u = cJSON_GetObjectItem(opus, "utilization");
        if (u && cJSON_IsNumber(u))
            out->opus_util = u->valuedouble;
    }
    cJSON *sonnet = cJSON_GetObjectItem(root, "seven_day_sonnet");
    if (sonnet && !cJSON_IsNull(sonnet)) {
        cJSON *u = cJSON_GetObjectItem(sonnet, "utilization");
        if (u && cJSON_IsNumber(u))
            out->sonnet_util = u->valuedouble;
    }

    /* Extra usage / credits */
    cJSON *extra = cJSON_GetObjectItem(root, "extra_usage");
    if (extra && !cJSON_IsNull(extra)) {
        out->extra_enabled = TRUE;
        cJSON *limit = cJSON_GetObjectItem(extra, "monthly_limit");
        if (limit && cJSON_IsNumber(limit))
            out->extra_limit = limit->valuedouble;
        cJSON *used = cJSON_GetObjectItem(extra, "used_credits");
        if (used && cJSON_IsNumber(used))
            out->extra_used = used->valuedouble;
    }

    out->valid = TRUE;
    cJSON_Delete(root);
}
