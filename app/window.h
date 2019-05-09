#ifndef WINDOW_H
#define WINDOW_H

#include "appapi.h"

#define WM_APP_INSTANCE_NOTIFY  (WM_APP + 0)
#define WM_APP_NODE_CONNECTION  (WM_APP + 1)
#define WM_APP_TITLE_RESOLVED   (WM_APP + 2)
#define WM_APP_CHANNELS_COUNT   (WM_APP + 3)
#define WM_APP_IDENTITIES_COUNT (WM_APP + 4)

enum IconSize {
    SmallIconSize = 0,
    StartIconSize,
    LargeIconSize,
    ShellIconSize,
    JumboIconSize,
    IconSizesCount
};

struct WindowPublic {
    HWND hWnd;
    long dpi = 96;
    int  metrics [SM_CMETRICS];

    HWND hToolTip = NULL;
    HICON icons [IconSizesCount] = { NULL, NULL, NULL, NULL, NULL };

    explicit inline WindowPublic (HWND hWnd)
        : hWnd (hWnd)
        , dpi (GetDPI (hWnd)) {};
};

#endif
