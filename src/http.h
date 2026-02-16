#ifndef HTTP_H
#define HTTP_H

#include <windows.h>
#include <winhttp.h>

typedef struct {
    int    status_code;   /* HTTP status (200, 401, etc.), 0 on connection failure */
    char  *body;          /* Heap-allocated response body (caller frees), NULL on failure */
    DWORD  body_len;
    DWORD  error_code;    /* Win32 error on failure, 0 on success */
} HttpResponse;

/* Initialize the HTTP session. Call once at startup. */
BOOL http_init(void);

/* Shutdown the HTTP session. Call once at exit. */
void http_shutdown(void);

/* Perform an HTTPS GET request.
   host: e.g. L"api.anthropic.com"
   url_path: e.g. L"/api/oauth/usage"
   headers: additional headers, \r\n separated
   Caller must call http_response_free(). */
HttpResponse http_get(const wchar_t *host, INTERNET_PORT port,
                      const wchar_t *url_path, const wchar_t *headers);

void http_response_free(HttpResponse *resp);

#endif
