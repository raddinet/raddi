#ifndef RADDI_WINDOW_ENVIRONMENT_H
#define RADDI_WINDOW_ENVIRONMENT_H

#include "../common/appapi.h"

#define WM_APP_INSTANCE_NOTIFY  (WM_APP + 0)
#define WM_APP_FINISH_COMMAND   (WM_APP + 1)
#define WM_APP_GUI_THEME_CHANGE (WM_APP + 2)

#define WM_APP_NODE_STATE       (WM_APP + 10)
#define WM_APP_NODE_UPDATE      (WM_APP + 11) // ...reserved to WM_APP+14

enum class IconSize : std::size_t {
    Small = 0,
    Start,
    Large,
    Shell,
    Jumbo,
    Count
};

struct WindowEnvironment {
    long dpi = 96;
    int  metrics [SM_CMETRICS];

    HWND hToolTip = NULL;
    HICON icons [(std::size_t) IconSize::Count] = { NULL, NULL, NULL, NULL, NULL };

    explicit inline WindowEnvironment (HWND hWnd)
        : dpi (GetDPI (hWnd)) {

        std::memset (this->metrics, 0, sizeof this->metrics);
    };

    SIZE GetIconMetrics (IconSize size, UINT dpiNULL = GetDPI (NULL));
    LRESULT RefreshVisualMetrics (UINT dpiNULL = GetDPI (NULL));

public:
    struct Font {
        HFONT handle = NULL;
        long  height = 0;
        
        SHORT width = FW_DONTCARE;
        bool make_italic = false;
        bool make_underlined = false;

        ~Font ();
        bool Update (HTHEME hTheme, UINT dpi, UINT dpiNULL, int id, const wchar_t * replace = nullptr, int m = 0, int d = 0);
    private:
        bool Update (const LOGFONT & lf);
    };
};

#endif
