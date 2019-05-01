#ifndef WINDOW_H
#define WINDOW_H

#include "appapi.h"

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
