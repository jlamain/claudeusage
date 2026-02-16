#include "http.h"
#include <stdlib.h>
#include <string.h>

/* Global session handle.
 *
 * Why global and persistent:
 * - Creating a session is expensive (involves registry lookups, proxy detection)
 * - We make requests every 60 seconds to the same host
 * - Reusing the session enables connection pooling and keep-alive
 * - WinHTTP is documented as thread-safe for separate handles, but since
 *   we're single-threaded, we don't need any synchronization
 */
static HINTERNET g_session = NULL;

/* Initialize the HTTP subsystem.
 *
 * Why open the session here instead of lazily:
 * - Allows us to fail fast at startup if HTTP is broken (e.g., missing DLLs)
 * - Session setup can take 100-500ms on some systems (proxy detection), so
 *   doing it at startup avoids a delay on the first API call
 *
 * Why WINHTTP_ACCESS_TYPE_DEFAULT_PROXY:
 * - Automatically respects system proxy settings (Internet Options → Connections)
 * - Handles corporate proxies, VPNs, and WPAD auto-configuration
 * - Alternative would be WINHTTP_ACCESS_TYPE_NO_PROXY which breaks in
 *   corporate environments
 */
BOOL http_init(void)
{
    g_session = WinHttpOpen(L"ClaudeUsage/1.0",  /* User-Agent for server logs */
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    return (g_session != NULL);
}

/* Cleanup the HTTP subsystem.
 *
 * Why check for NULL:
 * - In case init failed, don't try to close an invalid handle
 * - Setting to NULL after close prevents double-free if called twice
 */
void http_shutdown(void)
{
    if (g_session) {
        WinHttpCloseHandle(g_session);
        g_session = NULL;
    }
}

/* Perform a synchronous HTTPS GET request.
 *
 * Why synchronous instead of async:
 * - This runs on a timer in a hidden window - there's no UI to keep responsive
 * - Async WinHTTP requires setting up callbacks and handling state transitions
 * - For a simple tray app making one request per minute, blocking is fine
 * - The main thread will block for ~1-2 seconds during the request, but since
 *   there's no visible window, the user never notices
 *
 * Why separate hConnect and hRequest handles:
 * - This is the required WinHTTP calling pattern
 * - WinHttpConnect creates a connection to a host (or reuses one from the pool)
 * - WinHttpOpenRequest creates a specific request on that connection
 * - Both must be closed separately to avoid resource leaks
 *
 * Why WINHTTP_FLAG_SECURE:
 * - Enables TLS/SSL for HTTPS
 * - WinHTTP validates the server certificate automatically
 * - Uses the system's trusted root certificate store
 * - Will fail if the certificate is invalid (expired, wrong domain, self-signed)
 */
HttpResponse http_get(const wchar_t *host, INTERNET_PORT port,
                      const wchar_t *url_path, const wchar_t *headers)
{
    HttpResponse resp;
    memset(&resp, 0, sizeof(resp));

    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;

    /* Create connection to host (reuses existing connection if available) */
    hConnect = WinHttpConnect(g_session, host, port, 0);
    if (!hConnect) {
        resp.error_code = GetLastError();
        goto cleanup;
    }

    /* Create GET request */
    hRequest = WinHttpOpenRequest(hConnect, L"GET", url_path, NULL,
                                  WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        resp.error_code = GetLastError();
        goto cleanup;
    }

    /* Add custom headers (OAuth bearer token, anthropic-beta header) */
    if (headers && headers[0]) {
        /* -1L means "headers is null-terminated, calculate length" */
        WinHttpAddRequestHeaders(hRequest, (LPWSTR)headers, (DWORD)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD);
    }

    /* Set timeouts to fail fast on network issues.
     *
     * Why these specific values:
     * - 10s resolve: DNS is usually < 1s, but slow DNS can hit 5-8s
     * - 10s connect: TLS handshake + TCP handshake, can be slow on bad networks
     * - 10s send: our request is tiny (< 1KB), so this should never timeout
     * - 15s receive: JSON response is small (~500 bytes), but give extra time
     *   for server processing and slow network
     *
     * Why fail fast instead of infinite timeout:
     * - If the network is down, we want to know immediately (show error balloon)
     * - Next poll cycle will retry anyway
     * - Don't want to block the UI thread indefinitely
     */
    WinHttpSetTimeouts(hRequest, 10000, 10000, 10000, 15000);

    /* Send the request (headers + empty body for GET) */
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        resp.error_code = GetLastError();
        goto cleanup;
    }

    /* Wait for and receive the response headers */
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        resp.error_code = GetLastError();
        goto cleanup;
    }

    /* Extract HTTP status code (200, 401, etc.)
     *
     * Why query as number instead of string:
     * - Saves a string-to-int conversion
     * - WINHTTP_QUERY_FLAG_NUMBER tells WinHTTP to parse it for us
     *
     * Why not check the return value:
     * - If the query fails, status_code stays 0
     * - Caller interprets 0 as "unknown/error" which is correct
     */
    DWORD status_code = 0;
    DWORD size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &status_code, &size, WINHTTP_NO_HEADER_INDEX);
    resp.status_code = (int)status_code;

    /* Read response body in a loop.
     *
     * Why dynamic buffer with doubling:
     * - We don't know the response size in advance (no Content-Length header required)
     * - Starting at 4KB because typical API response is ~500 bytes
     * - Doubling strategy (4K→8K→16K) minimizes realloc calls
     * - Adding +1 for null terminator so cJSON can parse it as a string
     *
     * Why QueryDataAvailable + ReadData loop:
     * - This is the documented WinHTTP pattern for reading responses
     * - QueryDataAvailable tells us how many bytes are ready without blocking
     * - ReadData reads those bytes
     * - Loop continues until QueryDataAvailable returns 0 (EOF)
     */
    DWORD buf_cap = 4096;
    DWORD buf_len = 0;
    char *buf = (char *)malloc(buf_cap);
    if (!buf) {
        resp.error_code = ERROR_NOT_ENOUGH_MEMORY;
        goto cleanup;
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available))
            break;  /* Error or connection closed */
        if (available == 0)
            break;  /* EOF */

        /* Grow buffer if needed (ensure space for data + null terminator) */
        while (buf_len + available + 1 > buf_cap) {
            buf_cap *= 2;
            char *newbuf = (char *)realloc(buf, buf_cap);
            if (!newbuf) {
                /* realloc failed - free original and bail out */
                free(buf);
                buf = NULL;
                break;
            }
            buf = newbuf;
        }
        if (!buf) break;

        /* Read the available bytes */
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buf + buf_len, available, &bytesRead))
            break;
        buf_len += bytesRead;
    }

    /* Null-terminate for cJSON (it expects a C string) */
    if (buf) {
        buf[buf_len] = '\0';
        resp.body = buf;
        resp.body_len = buf_len;
    }

cleanup:
    /* Close handles in reverse order of creation.
     *
     * Why this order:
     * - Request handle must be closed before connection handle
     * - Connection handle must be closed before session handle (but session
     *   is global and closed separately in http_shutdown)
     * - Closing in wrong order can cause resource leaks or crashes
     */
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    return resp;
}

/* Free heap-allocated response body.
 *
 * Why a separate function instead of expecting caller to free():
 * - Encapsulation: caller doesn't need to know body is malloc'd
 * - Future-proofing: if we change to a different allocator or pool,
 *   only this function needs to change
 * - Symmetric with http_get() for API clarity
 */
void http_response_free(HttpResponse *resp)
{
    if (resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
    resp->body_len = 0;
}
