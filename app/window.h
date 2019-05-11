#ifndef WINDOW_H
#define WINDOW_H

#include "appapi.h"
#include "property.h"
#include "tabs.h"
#include "view.h"
#include "list.h"

#define WM_APP_INSTANCE_NOTIFY  (WM_APP + 0)
#define WM_APP_NODE_CONNECTION  (WM_APP + 1)
#define WM_APP_TITLE_RESOLVED   (WM_APP + 2)
#define WM_APP_CHANNELS_COUNT   (WM_APP + 3)
#define WM_APP_IDENTITIES_COUNT (WM_APP + 4)
#define WM_APP_FINISH_COMMAND   (WM_APP + 5)
#define WM_APP_GUI_THEME_CHANGE (WM_APP + 6)

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

class Window : public WindowPublic {
    LPARAM id;

    bool active = false;
    long extension = 0;
    long height = 0;

    // TODO: remove unused IDs, keep only those needed for callbacks
    struct ID {
        enum {
            ADD_TAB_BUTTON = 0x1001,
            OVERFLOW_BUTTON = 0x1008,
            HISTORY_BUTTON = 0x1009,
            LIST_OVERFLOW_BUTTON = 0x1038,

            TABS_VIEWS = 0x2001,
            TABS_LISTS = 0x2002,
            TABS_FEEDS = 0x2003,

            LIST_FIRST = 0x3000,
            LIST_LAST = 0x3FF0,
            LIST_IDENTITIES = 0x3FFF,
            LIST_CHANNELS = 0x3FFE,

            IDENTITIES = 0x4001,
            FILTERS = 0x4002,
            FEED_RECENT = 0x4010,
            FEED_FRIENDS = 0x4011,
            FEED_TWEETS = 0x4012,

            // TODO: mod/ban management controls

            STATUSBAR = 0xF000,
        };
    };

    union {
        struct {
            TabControlInterface * views = nullptr;
            TabControlInterface * lists = nullptr;
            TabControlInterface * feeds = nullptr;
        } tabs;
        TabControlInterface * alltabs [sizeof tabs / sizeof (TabControlInterface *)];
    };
    struct {
        ListOfChannels * channels = nullptr;
        ListOfIdentities * identities = nullptr;
    } lists;

    struct Fonts {
        struct Font {
            HFONT handle = NULL;
            long  height = 0;

            ~Font ();
            bool Update (HTHEME hTheme, UINT dpi, UINT dpiNULL, int id, const wchar_t * replace = nullptr, int m = 0, int d = 0);
        } text
        , tabs
        , tiny;
    } fonts;

    // TODO: on window resize, keep horz fixed (restrict min size), percentually adjust all vertical ones (but with limit to see one row)
    struct {
        property <long> left;
        property <long> right;
        property <long> right_restore;
        property <double> feeds;
    } dividers;

    struct {
        int what; // divider ID
        int x;
        int y;
    } drag;

    struct {
        SIZE statusbar = { 0, 0 };
    } minimum;

private:
    LRESULT Dispatch (UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT OnCreate (const CREATESTRUCT *);
    LRESULT OnDestroy ();
    LRESULT OnPositionChange (const WINDOWPOS &);
    LRESULT OnNotify (NMHDR *);
    LRESULT OnCommand (UINT notification, UINT child, HWND control);
    LRESULT OnNonClientActivate (UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnNonClientHitTest (UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnMouse (UINT message, WPARAM modifiers, LONG x, LONG y);
    LRESULT OnContextMenu (HWND hChild, LONG x, LONG y);
    LRESULT OnNcCalcSize (WPARAM, RECT *);
    LRESULT OnGetMinMaxInfo (MINMAXINFO *);
    LRESULT OnPaint ();
    LRESULT OnControlPrePaint (HDC hDC, HWND hControl);
    LRESULT OnDpiChange (WPARAM dpi, const RECT * target);
    LRESULT OnNodeConnectionUpdate (WPARAM information, LPARAM parameter);
    LRESULT OnVisualEnvironmentChange ();
    LRESULT OnFinishCommand (WPARAM action, LPARAM parameter);
    LRESULT RefreshVisualMetrics (UINT dpiNULL = GetDPI (NULL));

    void AssignHint (HWND hCtrl, UINT string);

    std::intptr_t CreateTab (const raddi::eid & entry, const std::wstring & text, std::intptr_t id = 0);
    std::intptr_t CreateTab (const raddi::eid & entry, std::intptr_t id = 0);
    void CloseTab (std::intptr_t id);

    const MARGINS * GetDwmMargins ();
    RECT GetListsTabRect ();
    RECT GetFeedsTabRect (const RECT &);
    RECT GetListsFrame (const RECT *, const RECT & rListTabs);
    RECT GetViewsFrame (const RECT *);
    RECT GetRightPane (const RECT & client, const RECT & rListTabs);
    RECT GetFeedsFrame (const RECT *, const RECT & rFeedsTabs);
    RECT GetFiltersRect (const RECT * rcArea, const RECT & rRightPane);

    RECT GetTabControlClipRect (RECT);
    RECT GetTabControlContentRect (RECT);
    SIZE GetIconMetrics (IconSize size, UINT dpiNULL = GetDPI (NULL));
    void BackgroundFill (HDC hDC, const RECT * rcArea, const RECT * rcClip, bool fromControl);
    LONG UpdateStatusBar (HWND hStatusBar, UINT dpi, const RECT & rParent);
    void UpdateListsPosition (HDWP &, const RECT &, const RECT & rListTabs);
    void UpdateViewsPosition (HDWP &, const RECT &);
    void UpdateFeedsPosition (HDWP &, const RECT &, const RECT & rFeedsTabs);
    bool GetCaptionTextColor (COLORREF & color, UINT & glow) const;

    std::intptr_t TabIdFromContentMenu (LONG & x, LONG & y, TabControlInterface * tc);

    static LRESULT CALLBACK Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK InitialProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK DefaultProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL * bResultFromDWM = NULL);
public:
    static LPCTSTR Initialize (HINSTANCE);
    explicit Window (HWND hWnd, WPARAM wParam, LPARAM lParam, CREATESTRUCT * cs);
};

inline std::wstring LoadString (unsigned int id) {
    return raddi::log::translate (raddi::log::rsrc_string (id), std::wstring ());
}

#endif
