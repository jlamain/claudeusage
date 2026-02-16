#ifndef POPUP_H
#define POPUP_H

#include <windows.h>
#include "api.h"

/* Register the popup window class. Call once at startup. */
void popup_register(HINSTANCE hInstance);

/* Show the detail popup near the tray icon.
   If already visible, brings to foreground and repaints. */
void popup_show(HINSTANCE hInstance, const UsageData *usage);

/* Hide and destroy the popup if visible. */
void popup_hide(void);

#endif
