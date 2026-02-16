#include "config.h"
#include "cJSON.h"
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_get_dir(wchar_t *path, int max_len)
{
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    _snwprintf(path, max_len, L"%s\\claudeusage", appdata);
}

static void config_get_path(wchar_t *path, int max_len)
{
    wchar_t dir[MAX_PATH_LEN];
    config_get_dir(dir, MAX_PATH_LEN);
    _snwprintf(path, max_len, L"%s\\config.ini", dir);
}

static BOOL file_exists(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static BOOL try_find_credentials(wchar_t *out, int max_len)
{
    wchar_t path[MAX_PATH_LEN];

    /* Check %USERPROFILE%\.claude\.credentials.json */
    wchar_t userprofile[MAX_PATH];
    if (GetEnvironmentVariableW(L"USERPROFILE", userprofile, MAX_PATH) > 0) {
        _snwprintf(path, MAX_PATH_LEN, L"%s\\.claude\\.credentials.json", userprofile);
        if (file_exists(path)) {
            wcsncpy(out, path, max_len);
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL parse_config_file(const wchar_t *config_path, AppConfig *cfg)
{
    HANDLE hFile = CreateFileW(config_path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    char buf[4096];
    DWORD bytesRead;
    if (!ReadFile(hFile, buf, sizeof(buf) - 1, &bytesRead, NULL)) {
        CloseHandle(hFile);
        return FALSE;
    }
    CloseHandle(hFile);
    buf[bytesRead] = '\0';

    /* Parse line by line */
    char *line = strtok(buf, "\r\n");
    while (line) {
        /* Skip comments and empty lines */
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '#' || *line == ';' || *line == '\0') {
            line = strtok(NULL, "\r\n");
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;
            /* Trim key */
            while (*key == ' ') key++;
            char *kend = key + strlen(key) - 1;
            while (kend > key && *kend == ' ') *kend-- = '\0';
            /* Trim value */
            while (*val == ' ') val++;
            char *vend = val + strlen(val) - 1;
            while (vend > val && (*vend == ' ' || *vend == '\r' || *vend == '\n'))
                *vend-- = '\0';

            if (strcmp(key, "credentials_path") == 0) {
                wchar_t *w = (wchar_t *)malloc(MAX_PATH_LEN * sizeof(wchar_t));
                if (w) {
                    MultiByteToWideChar(CP_UTF8, 0, val, -1, w, MAX_PATH_LEN);
                    wcsncpy(cfg->credentials_path, w, MAX_PATH_LEN - 1);
                    free(w);
                }
            } else if (strcmp(key, "poll_interval") == 0) {
                int v = atoi(val);
                if (v > 0) cfg->poll_interval_sec = v;
            }
        }
        line = strtok(NULL, "\r\n");
    }

    return (cfg->credentials_path[0] != L'\0');
}

static void write_template_config(const wchar_t *config_path, const wchar_t *cred_path)
{
    wchar_t dir[MAX_PATH_LEN];
    config_get_dir(dir, MAX_PATH_LEN);
    CreateDirectoryW(dir, NULL);

    HANDLE hFile = CreateFileW(config_path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    char content[2048];
    char cred_narrow[MAX_PATH_LEN];
    WideCharToMultiByte(CP_UTF8, 0, cred_path, -1, cred_narrow, MAX_PATH_LEN, NULL, NULL);

    snprintf(content, sizeof(content),
        "# Claude Usage Tray - Configuration\n"
        "#\n"
        "# Path to Claude Code .credentials.json file\n"
        "# Example: C:\\Users\\<user>\\.claude\\.credentials.json\n"
        "credentials_path=%s\n"
        "\n"
        "# Poll interval in seconds (default: 60)\n"
        "poll_interval=60\n",
        cred_narrow);

    DWORD written;
    WriteFile(hFile, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(hFile);
}

BOOL config_load(AppConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->poll_interval_sec = 60;

    wchar_t config_path[MAX_PATH_LEN];
    config_get_path(config_path, MAX_PATH_LEN);

    /* Try loading existing config */
    if (file_exists(config_path) && parse_config_file(config_path, cfg))
        return TRUE;

    /* First run: try to auto-detect credentials */
    wchar_t cred_path[MAX_PATH_LEN] = {0};
    if (try_find_credentials(cred_path, MAX_PATH_LEN)) {
        wcsncpy(cfg->credentials_path, cred_path, MAX_PATH_LEN - 1);
        write_template_config(config_path, cred_path);
        return TRUE;
    }

    /* No credentials found - write template and tell user */
    write_template_config(config_path,
        L"C:\\Users\\<user>\\.claude\\.credentials.json");

    wchar_t msg[1024];
    _snwprintf(msg, 1024,
        L"Claude Usage Tray needs your Claude Code credentials.\n\n"
        L"Please edit the config file and set the path to your "
        L".credentials.json file:\n\n%s",
        config_path);
    MessageBoxW(NULL, msg, L"Claude Usage - First Run Setup",
                MB_OK | MB_ICONINFORMATION);

    /* Open config in Notepad */
    ShellExecuteW(NULL, L"open", L"notepad.exe", config_path, NULL, SW_SHOW);
    return FALSE;
}

BOOL config_read_access_token(const wchar_t *credentials_path,
                              char *token, int token_len)
{
    HANDLE hFile = CreateFileW(credentials_path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize > 8192) {
        CloseHandle(hFile);
        return FALSE;
    }

    char *buf = (char *)malloc(fileSize + 1);
    if (!buf) { CloseHandle(hFile); return FALSE; }

    DWORD bytesRead;
    if (!ReadFile(hFile, buf, fileSize, &bytesRead, NULL)) {
        free(buf);
        CloseHandle(hFile);
        return FALSE;
    }
    CloseHandle(hFile);
    buf[bytesRead] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return FALSE;

    cJSON *oauth = cJSON_GetObjectItem(root, "claudeAiOauth");
    if (!oauth) { cJSON_Delete(root); return FALSE; }

    cJSON *at = cJSON_GetObjectItem(oauth, "accessToken");
    if (!at || !cJSON_IsString(at)) { cJSON_Delete(root); return FALSE; }

    strncpy(token, at->valuestring, token_len - 1);
    token[token_len - 1] = '\0';

    cJSON_Delete(root);
    return TRUE;
}
