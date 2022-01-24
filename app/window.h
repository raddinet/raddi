#ifndef WINDOW_H
#define WINDOW_H

#include "../common/window_environment.h"
#include "property.h"
#include "tabs.h"
#include "view.h"
#include "lists.h"
#include "channels.h"

#define WM_APP_TITLE_RESOLVED   (WM_APP + 3) // TODO: remove?
//#define WM_APP_CHANNELS_COUNT   (WM_APP + 3)
//#define WM_APP_IDENTITIES_COUNT (WM_APP + 4)

// TODO: AppWindow : Window : WindowEnvironment
//  - appwindow.h, common/window.h, common/window_environment.h

class Window : public WindowEnvironment {
public:
    const HWND hWnd;
    const LPARAM id;

    // TODO: remove unused IDs, keep only those needed for callbacks
    struct ID {
        static constexpr auto MAX_LISTS = 0x1F0;
        // static constexpr auto MAX_LIST_GROUPS = 0x1F0;
        enum {
            ADD_TAB_BUTTON = 0x1001,
            OVERFLOW_BUTTON = 0x1008,
            HISTORY_BUTTON = 0x1009,
            LIST_OVERFLOW_BUTTON = 0x1038,

            TABS_VIEWS = 0x2001,
            TABS_LISTS = 0x2002,
            TABS_FEEDS = 0x2003,

            LIST_BASE = 0x3000,
            LIST_LAST = LIST_BASE + MAX_LISTS,
            LIST_CHANNELS = 0x31FE,
            LIST_IDENTITIES = 0x31FF,

            LIST_SUBMENU_BASE = 0x3200,
            LIST_SUBMENU_LAST = 0x4000,

            IDENTITIES = 0x4001,
            FILTERS = 0x4002,
            FEED_RECENT = 0x4010,
            FEED_FRIENDS = 0x4011,
            FEED_TWEETS = 0x4012,

            // TODO: mod/ban management controls

            STATUSBAR = 0xF000,
        };
    };

private:
    bool active = false;
    long extension = 0;
    long height = 0;

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
        ListOfChannels * identities = nullptr;
    } lists;

public:
    struct Fonts {
        Font text;
        Font tabs;
        Font tiny;

        Font italic;
        Font underlined;
    } fonts;

private:
    // TODO: on window resize, keep horz fixed (restrict min size), percentually adjust all vertical ones (but with limit to see one row)
    struct {
        property <long> left;
        property <long> right;
        property <long> right_restore;
        property <double> feeds;
    } dividers;

    struct {
        int what = 0; // divider ID
        int x = 0;
        int y = 0;
    } drag;

    struct {
        SIZE statusbar = { 0, 0 };
    } minimum;

public:
    enum class FinishCommand {
        RefreshList
    };

    void FinishCommandInAllWindows (LPARAM command) const;
    void FinishCommandInAllWindows (FinishCommand command) const {
        this->FinishCommandInAllWindows (0x10000 + (LPARAM) command); // not an ID
    }

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
    LRESULT OnDrawItem (WPARAM id, DRAWITEMSTRUCT *);
    LRESULT OnControlPrePaint (HDC hDC, HWND hControl);
    LRESULT OnDpiChange (WPARAM dpi, const RECT * target);
    LRESULT OnNodeConnectionUpdate (WPARAM information, LPARAM parameter);
    LRESULT OnVisualEnvironmentChange ();
    LRESULT OnFinishCommand (LPARAM action);
    LRESULT OnAppEidResolved (UINT child);

    void AssignHint (HWND hCtrl, UINT string);

    int CreateTab (const raddi::eid & entry, const std::wstring & text, int id = 0);
    int CreateTab (const raddi::eid & entry, int id = 0);
    void CloseTab (int id);
    void CloseTabStack (int id); // tab id

    const MARGINS * GetDwmMargins ();
    RECT GetListsTabRect ();
    RECT GetFeedsTabRect (const RECT *);
    RECT GetAdjustedFrame (RECT r, LONG top, const RECT & rTabs);
    RECT GetListsFrame (const RECT *, const RECT & rListTabs);
    RECT GetViewsFrame (const RECT *);
    RECT GetRightPane (const RECT * client, const RECT & rListTabs);
    RECT GetFeedsFrame (const RECT *, const RECT & rFeedsTabs);
    RECT GetFiltersRect (const RECT * rcArea, const RECT & rRightPane);

    RECT GetTabControlClipRect (RECT);
    RECT GetTabControlContentRect (RECT);
    void BackgroundFill (HDC hDC, RECT rcArea, const RECT * rcClip, bool fromControl);
    LONG UpdateStatusBar (HWND hStatusBar, UINT dpi, const RECT & rParent);
    void UpdateListsPosition (HDWP &, const RECT &, const RECT & rListTabs);
    void UpdateViewsPosition (HDWP &, const RECT &);
    void UpdateFeedsPosition (HDWP &, const RECT &, const RECT & rFeedsTabs);
    void Reposition ();
    bool GetCaptionTextColor (HTHEME hTheme, COLORREF & color, UINT & glow) const;

    std::intptr_t TabIdFromContentMenu (LONG * x, LONG * y, TabControlInterface * tc);
    std::intptr_t FindFirstAvailableListId () const;
    std::intptr_t GetUserListIdFromIndex (std::size_t index) const;
    std::intptr_t GetUserListIndexById (std::size_t id) const;

    static LRESULT CALLBACK Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK InitialProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK DefaultProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL * bResultFromDWM = NULL);
public:
    static LPCTSTR Initialize (HINSTANCE);
    explicit Window (HWND hWnd, CREATESTRUCT * cs);
};

inline std::wstring LoadString (unsigned int id) {
    return raddi::log::translate (raddi::log::rsrc_string (id), std::wstring ());
}

#endif
