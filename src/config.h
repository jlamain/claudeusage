#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

#define MAX_PATH_LEN  1024
#define MAX_TOKEN_LEN 512

typedef struct {
    wchar_t credentials_path[MAX_PATH_LEN];
    int     poll_interval_sec;  /* default 60 */
} AppConfig;

/* Load config from %APPDATA%\claudeusage\config.ini.
   On first run, tries to auto-detect credentials path.
   Returns TRUE if a valid config was loaded or created. */
BOOL config_load(AppConfig *cfg);

/* Read the OAuth access token from the credentials JSON file.
   Returns TRUE if token was successfully extracted. */
BOOL config_read_access_token(const wchar_t *credentials_path,
                              char *token, int token_len);

/* Read the subscription type from the credentials JSON file.
   Returns TRUE if subscription type was successfully extracted. */
BOOL config_read_subscription_type(const wchar_t *credentials_path,
                                   char *type, int type_len);

/* Get the config directory path (%APPDATA%\claudeusage\). */
void config_get_dir(wchar_t *path, int max_len);

#endif
