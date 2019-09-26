#include "window.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <dwmapi.h>

#include <VersionHelpers.h>

#include "menus.h"
#include "feed.h"
#include "data.h"
#include "../common/errorbox.h"
#include "editbox.h"
#include "listview_helpers.h"
#include "../common/node.h"
#include "resolver.h"
#include "prompts.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern "C" const IID IID_IImageList;

extern Design design;
extern Cursors cursor;
extern Resolver resolver;

namespace {
    LRESULT CALLBACK DisableWheelSubclassProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);
    LRESULT CALLBACK AlphaSubclassProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);
}

LPCTSTR Window::Initialize (HINSTANCE hInstance) {
    WNDCLASSEX wndclass = {
        sizeof (WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
        InitialProcedure, 0, 0, hInstance,  NULL,
        NULL, NULL, NULL, L"RADDI", NULL
    };
    return (LPCTSTR) (std::intptr_t) RegisterClassEx (&wndclass);
}

Window::Window (HWND hWnd, CREATESTRUCT * cs)
    : WindowEnvironment (hWnd)
    , hWnd (hWnd)
    , id ((LPARAM) cs->lpCreateParams)
    , dividers {
        { id, 0x201, long (256 * dpi / 96) }, // left
        { id, 0x202, long (256 * dpi / 96) }, // right
        { id, 0x203, 0 }, // right_restore
        { id, 0x103, double (256 * dpi / 96) }, // feeds
} {

    // for some reason these are not initialized and cause exception/crash in OnPositionChange
    this->tabs.views = nullptr;
    this->tabs.lists = nullptr;
    this->tabs.feeds = nullptr;
}

LRESULT Window::Dispatch (UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCREATE:
            if (ptrEnableNonClientDpiScaling) {
                ptrEnableNonClientDpiScaling (hWnd);
            }
            RefreshVisualMetrics ();
            return TRUE;

        case WM_CREATE:
            try {
                return OnCreate (reinterpret_cast <const CREATESTRUCT *> (lParam));
            } catch (const std::bad_alloc &) {
                ErrorBox (0x05);
                return -1;
            }
        case WM_DESTROY:
            return OnDestroy ();

        case WM_NCDESTROY:
            delete this; // imagine meme of Mark Zuckerberg pointing a gun at you
            return 0;

        case WM_ENDSESSION:
            if (wParam) {
                // TODO: ShutdownBlockReasonCreate if there are unsent requests, and in that time refuse WM_QUERYENDSESSION (on XP show message box)
                DestroyWindow (hWnd);
            }
            break;

        case WM_SYSCOMMAND:
            if ((wParam == SC_CLOSE) && (lParam == 0) && !(GetKeyState (VK_MENU) & 0x8000)) {
                // smart or remote close (double-click window menu, or from taskbar); don't ask, just stash
                DestroyWindow (hWnd);
                return 0;
            }
            break;

        case WM_CLOSE:
            if (!IsLastWindow (hWnd)) {
                switch (CloseWindowPrompt (hWnd)) {
                    case IDCANCEL: // keep open
                        return 0;
                    case IDYES: // stash is noop
                        break;
                    case IDNO:
                        // TODO: close all tabs
                        for (auto & query : database.windows.close) {
                            query.execute (this->id);
                        }
                        break;
                }
            }
            break;

        case WM_DPICHANGED:
            return OnDpiChange (wParam, reinterpret_cast <const RECT *> (lParam));
        case WM_WINDOWPOSCHANGED:
            return OnPositionChange (*reinterpret_cast <const WINDOWPOS *> (lParam));

        case WM_NCCALCSIZE:
            return OnNcCalcSize (wParam, reinterpret_cast <RECT *> (lParam));
        case WM_NCRBUTTONDOWN:
            switch (wParam) {
                case HTCAPTION:
                case HTSYSMENU:
                    SendMessage (hWnd, 787, 0, lParam); // WM_SYSMENU
                    break;
            }
            break;
        case WM_NCHITTEST:
            return OnNonClientHitTest (message, wParam, lParam);
        case WM_NCACTIVATE:
            return OnNonClientActivate (message, wParam, lParam);
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_MOUSEMOVE:
            return OnMouse (message, wParam, (short) LOWORD (lParam), (short) HIWORD (lParam));
        case WM_MOUSELEAVE:
            return OnMouse (message, wParam, -1, -1);
        case WM_CONTEXTMENU:
            return OnContextMenu ((HWND) wParam, (short) LOWORD (lParam), (short) HIWORD (lParam));

        case WM_GETMINMAXINFO:
            return OnGetMinMaxInfo (reinterpret_cast <MINMAXINFO *> (lParam));

        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
        case WM_DWMCOMPOSITIONCHANGED:
            PostMessage (NULL, WM_APP_GUI_THEME_CHANGE, 0, 0);
            break;
        case WM_APP_GUI_THEME_CHANGE:
            this->OnVisualEnvironmentChange ();
            InvalidateRect (hWnd, NULL, TRUE);
            break;

        case WM_CTLCOLORSTATIC:
            SetBkMode ((HDC) wParam, TRANSPARENT);
        case WM_CTLCOLORBTN:
            return OnControlPrePaint ((HDC) wParam, (HWND) lParam);

        case WM_PRINTCLIENT:
        case WM_ERASEBKGND:
            if (WindowFromDC ((HDC) wParam) != hWnd) {
                RECT client;
                if (GetClientRect (hWnd, &client)) {
                    BackgroundFill ((HDC) wParam, &client, &client, false);
                }
                return true;
            } else
                return false;

        case WM_PAINT:
            if (wParam)
                return this->Dispatch (WM_PRINTCLIENT, wParam, lParam);
            else
                return OnPaint ();

        case WM_COMMAND:
            return OnCommand (HIWORD (wParam), LOWORD (wParam), (HWND) lParam);
        case WM_NOTIFY:
            return OnNotify (reinterpret_cast <NMHDR *> (lParam));
        case WM_APPCOMMAND:
            switch (GET_APPCOMMAND_LPARAM (lParam)) {
                case APPCOMMAND_BROWSER_BACKWARD: break;
                case APPCOMMAND_BROWSER_FORWARD: break;
                case APPCOMMAND_BROWSER_HOME: break;
                case APPCOMMAND_BROWSER_FAVORITES: break;
                case APPCOMMAND_BROWSER_REFRESH: break;
                case APPCOMMAND_BROWSER_SEARCH: break;
                case APPCOMMAND_BROWSER_STOP: break; // ???
                case APPCOMMAND_MEDIA_CHANNEL_DOWN: break;
                case APPCOMMAND_MEDIA_CHANNEL_UP: break;
                case APPCOMMAND_PRINT: break;
                case APPCOMMAND_FIND: break;

                case APPCOMMAND_NEW:
                case APPCOMMAND_OPEN:
                    return this->OnCommand (0, 0xC0, NULL);
                case APPCOMMAND_CLOSE:
                    return this->Dispatch (WM_CLOSE, 0, 0);
            }
            break;

        case WM_APP_FINISH_COMMAND:
            return this->OnFinishCommand (lParam);
        case WM_APP_TITLE_RESOLVED:
            return this->OnAppEidResolved (wParam);
        case WM_APP_NODE_STATE:
            return OnNodeConnectionUpdate (wParam, lParam);
        /*case WM_APP_CHANNELS_COUNT:
            return this->lists.channels->Update (wParam, lParam);
        case WM_APP_IDENTITIES_COUNT:
            return this->lists.identities->Update (wParam);*/
    }
    return DefaultProcedure (this->hWnd, message, wParam, lParam);
}

LRESULT Window::OnNodeConnectionUpdate (WPARAM status, LPARAM parameter) {
    // TODO: update statusbar icons etc
    switch (status) {
        case 0: // disconnected
            break;
        case 1: // connected
            break;
    }

    this->lists.identities->Update ();
    this->lists.channels->Update ();
    return 0;
}

LRESULT Window::OnAppEidResolved (UINT child) {

    // TODO: forward to Views

    switch (child) {
        case ID::LIST_IDENTITIES:
            // this->lists.identities->Resolved (?)
            InvalidateRect (this->lists.identities->hWnd, NULL, FALSE);
            break;
        case ID::LIST_CHANNELS:
            // this->lists.channels->Resolved (?)
            InvalidateRect (this->lists.channels->hWnd, NULL, FALSE);
            break;
    }
    if ((child >= ID::LIST_BASE) && (child <= ID::LIST_LAST)) {
        // Lists::OnResolve (...)???
        InvalidateRect (GetDlgItem (this->hWnd, child), NULL, FALSE);
    }
    return 0;
}

std::intptr_t Window::CreateTab (const raddi::eid & entry, const std::wstring & text, std::intptr_t id) {
    bool reposition = false;
    if (id == 0) {
        id = this->tabs.views->tabs.crbegin ()->first + 1;
        if (id < 1) {
            id = 1;
        }
        reposition = true;
    }

    try {
        wchar_t szTmp [16];
        szTmp [std::swprintf (szTmp, 15, L"[%zu] ", id)] = 0;

        this->tabs.views->tabs [id].text = szTmp + text;
        this->tabs.views->tabs [id].icon = NULL; // TODO: loading animating ICO and EID in title

        this->tabs.views->tabs [id].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", szTmp,
                                                                WS_CHILD | WS_CLIPSIBLINGS,// | WS_BORDER,// | WS_HSCROLL | WS_VSCROLL,
                                                                0, 0, 0, 0, this->hWnd, NULL,
                                                                (HINSTANCE) GetWindowLongPtr (this->hWnd, GWLP_HINSTANCE), NULL);
        // View::Create (...)
        // this->tabs.views->tabs [id].progress = 1; // TODO: enqueue threadpool item to load the
        
        if (reposition) {
            this->Reposition ();
        }
        return id;
    } catch (const std::bad_alloc &) {
        return 0;
    }
}

std::intptr_t Window::CreateTab (const raddi::eid & entry, std::intptr_t id) {
    return this->CreateTab (entry, entry.serialize (), id);
}

void Window::CloseTab (std::intptr_t id) {
    if ((id > 0) && this->tabs.views->tabs.count (id)) {
        database.tabs.close [0] (this->id, id, this->tabs.views->tabs [id].text);
        database.tabs.close [1] (this->id, id);

        if (auto hCtrl = this->tabs.views->tabs [id].content) {
            DestroyWindow (hCtrl);
        }
        this->tabs.views->tabs.erase (id);
        this->tabs.views->update ();
    }
}

void Window::CloseTabStack (std::intptr_t id) {
    if ((id > 0) && this->tabs.views->tabs.count (id)) {
        auto index = this->tabs.views->tabs [id].stack_index;

        auto i = this->tabs.views->tabs.begin ();
        auto e = this->tabs.views->tabs.end ();

        while (i != e) {
            auto id = i->first;
            auto & tab = i->second;

            if (tab.stack_index == index) {
                database.tabs.close [0] (this->id, id, tab.text);
                database.tabs.close [1] (this->id, id);

                if (tab.content) {
                    DestroyWindow (tab.content);
                }

                i = this->tabs.views->tabs.erase (i);
            } else {
                ++i;
            }
        }
        this->tabs.views->update ();
    }
}

void Window::AssignHint (HWND hCtrl, UINT string) {
    TOOLINFO tt = {
        sizeof (TOOLINFO), TTF_IDISHWND | TTF_SUBCLASS,
        this->hWnd, (UINT_PTR) hCtrl, {0,0,0,0},
        NULL, LPSTR_TEXTCALLBACK, (LPARAM) string, NULL
    };
    SendMessage (this->hToolTip, TTM_ADDTOOL, 0, (LPARAM) &tt);
}

LRESULT Window::OnCreate (const CREATESTRUCT * cs) {
    this->hToolTip = CreateWindowEx (WS_EX_NOPARENTNOTIFY, TOOLTIPS_CLASS, L"",
                                        WS_POPUP | WS_VISIBLE | TTS_NOPREFIX, 0,0,0,0, hWnd,
                                        NULL, cs->hInstance, NULL);
    if (this->hToolTip) {
        SendMessage (this->hToolTip, CCM_DPISCALE, TRUE, 0);
        SetWindowPos (this->hToolTip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        // TTM_SETTITLE TTI_INFO
    }

    if (auto tc = (this->tabs.views = CreateTabControl (cs->hInstance, hWnd, WS_TABSTOP | WS_VISIBLE, ID::TABS_VIEWS))) {
        tc->hToolTipControl = this->hToolTip;
        tc->addbutton (ID::ADD_TAB_BUTTON, IsWindowsVistaOrGreater () ? L"\x271A" : L"+", 0x1A, false);
        tc->addbutton (ID::HISTORY_BUTTON, IsWindows10OrGreater () ? L"\xE2AC" : L"\x21BA", 0x1B, true); // TODO: What on XP? TODO: keyboard shortcut?
        tc->addbutton (ID::OVERFLOW_BUTTON, IsWindows10OrGreater () ? L"|||" : L"|||", 0x1C, true); // TODO: What on XP? TODO: keyboard shortcut?

        tc->stacking = true;
        tc->badges = true;

        tc->tabs [-4].text = L"\xE128"; // "\x2081\x2082"; // NET
        tc->tabs [-4].tip = LoadString (0x11);
        tc->tabs [-4].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"Network overview (something like /r/all?)",
                                                WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, NULL,
                                                cs->hInstance, NULL);
        tc->tabs [-3].text = L"\xE1CF"; // FAVorites
        tc->tabs [-3].tip = LoadString (0x12);
        tc->tabs [-3].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"Favorites",
                                                WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, NULL,
                                                cs->hInstance, NULL);
        tc->tabs [-2].text = L"\xE142"; // "\x2085\x2086"; // NOTification
        tc->tabs [-2].tip = LoadString (0x13);
        tc->tabs [-2].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"Notifications (TODO: Posted to identity channel)",
                                                WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, NULL,
                                                cs->hInstance, NULL);
        tc->tabs [-1].text = L"\xE205"; // "\x2089\x208A";// PRIvate messages
        tc->tabs [-1].tip = LoadString (0x14);
        tc->tabs [-1].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"Private messages",
                                                WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, NULL,
                                                cs->hInstance, NULL);
        for (auto & tab : tc->tabs) {
            tab.second.close = false;
            tab.second.fit = true;
        }

        try {
            std::map <std::intptr_t, std::intptr_t> last_stack_tab;

            database.tabs.query.bind (this->id);
            while (database.tabs.query.next ()) { // id, stack, entry, scroll, t

                raddi::eid entry = database.tabs.query.get <SQLite::Blob> (2);

                if (auto tab = this->CreateTab (entry, database.tabs.query.get <std::intptr_t> (0))) {
                    if (auto stack = database.tabs.query.get <std::intptr_t> (1)) {

                        auto ii = last_stack_tab.find (stack);
                        if (ii == last_stack_tab.end ()) {
                            last_stack_tab.insert ({ stack, tab });
                        } else {
                            tc->stack (tab, ii->second, false);
                            ii->second = tab;
                        }
                    }
                }
            }
        } catch (const SQLite::Exception & x) {
            ErrorBox (hWnd, raddi::log::level::error, 0x03, x.what ());
        }

        try {
            tc->request (database.current.get.query <std::intptr_t> (this->id, "tab"));
        } catch (const SQLite::InStatementException &) {
            // TODO: first open/run, insert and activate default blank tab
        }
    }
    if (auto tc = (this->tabs.lists = CreateTabControl (cs->hInstance, hWnd, WS_TABSTOP | WS_VISIBLE, ID::TABS_LISTS))) {
        tc->hToolTipControl = this->hToolTip;
        tc->addbutton (ID::LIST_OVERFLOW_BUTTON, IsWindows10OrGreater () ? L"|||" : L"|||", 0x2C, true); // TODO: What on XP? TODO: keyboard shortcut?

        try {
            if (!Lists::Load (this, tc)) {
                // ErrorBox (hWnd, raddi::log::level::error, 0x04, x.what ());
            }
        } catch (const SQLite::Exception & x) {
            ErrorBox (hWnd, raddi::log::level::error, 0x04, x.what ());
        }

        // TODO: optionally use images for these two also? people is obvious, channels perhaps grid of quads "app" icon

        try {
            this->lists.channels = new ListOfChannels (this, ID::LIST_CHANNELS, Node::table::channels);

            tc->tabs [-7].content = this->lists.channels->hWnd;
            tc->tabs [-7].text = LoadString (0x20);
            tc->tabs [-7].tip = LoadString (0x21);
            tc->update ();
        } catch (const std::bad_alloc &) {
            return -1;
        }

        try {
            this->lists.identities = new ListOfChannels (this, ID::LIST_IDENTITIES, Node::table::identities);

            tc->tabs [-3].content = this->lists.identities->hWnd;
            tc->tabs [-3].text = LoadString (0x22);
            tc->tabs [-3].tip = LoadString (0x23);
            tc->update ();
        } catch (const std::bad_alloc &) {
            return -1;
        }

        if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
            for (auto & tab : tc->tabs) {
                SetWindowSubclass (tab.second.content, AlphaSubclassProcedure, 0, 0);
                // SetWindowSubclass (ListView_GetHeader (tab.second.content), AlphaSubclassProcedure, 0, 0);
            }
        }
        for (auto & tab : tc->tabs) {
            tab.second.close = false;
        }

        try {
            tc->request (database.current.get.query <std::intptr_t> (this->id, "list"));
        } catch (const SQLite::InStatementException &) {
            tc->request (1); // TODO: activate first list?
        }
    }

    if (auto tc = (this->tabs.feeds = CreateTabControl (cs->hInstance, hWnd, WS_TABSTOP | WS_VISIBLE, ID::TABS_FEEDS))) {
        tc->hToolTipControl = this->hToolTip;
        tc->addbutton (0xF4, IsWindowsVistaOrGreater () ? L"\x25B6" : L">>", 0x3A, true);

        // tc->badges = true; // ???
        tc->tabs [1].text = LoadString (0x30);
        tc->tabs [1].tip = LoadString (0x31);
        tc->tabs [1].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"ACTIVITY",
                                                WS_CHILD | WS_CLIPSIBLINGS, 0,0,0,0, hWnd, (HMENU) ID::FEED_RECENT,
                                                cs->hInstance, NULL);
        tc->tabs [2].text = LoadString (0x32);
        tc->tabs [2].tip = LoadString (0x33);
        tc->tabs [2].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"FRIENDS",
                                                WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, (HMENU) ID::FEED_FRIENDS,
                                                cs->hInstance, NULL);
        tc->tabs [3].text = LoadString (0x34);
        tc->tabs [3].tip = LoadString (0x35);
        tc->tabs [3].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"TWEETS",
                                                WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, (HMENU) ID::FEED_TWEETS,
                                                cs->hInstance, NULL);

        if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
            for (auto & tab : tc->tabs) {
                SetWindowSubclass (tab.second.content, AlphaSubclassProcedure, 0, 0);
            }
        }
        for (auto & tab : tc->tabs) {
            tab.second.close = false;
        }

        std::intptr_t activate;
        try {
            activate = database.current.get.query <std::intptr_t> (this->id, "feed");
        } catch (const SQLite::InStatementException &) {
            activate = 1;
        }
        tc->request (activate);
    }

    if (auto hIdentities = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"COMBOBOX", L"",
                                            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                            0, 0, 0, 0, hWnd, (HMENU) ID::IDENTITIES, cs->hInstance, NULL)) {
            
        // database.identities.list
        SendMessage (hIdentities, CB_ADDSTRING, 0, (LPARAM) L"TEST TEST TEST");
        SendMessage (hIdentities, CB_ADDSTRING, 0, (LPARAM) L"AAA");
        SendMessage (hIdentities, CB_ADDSTRING, 0, (LPARAM) L"BBBBB");
        if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
            SetWindowSubclass (hIdentities, AlphaSubclassProcedure, 0, 2);
        }
        SetWindowSubclass (hIdentities, DisableWheelSubclassProcedure, 0, 0);
        this->AssignHint (hIdentities, 0x40);
    }

    // TODO: "manage" button aside of combobox?

    if (auto hFilters = CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"",
                                        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE |// WS_BORDER |
                                        LVS_LIST | LVS_EDITLABELS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS,
                                        0, 0, 0, 0, hWnd, (HMENU) ID::FILTERS, cs->hInstance, NULL)) {
            
        // LVS_EX_CHECKBOXES | LVS_EX_ONECLICKACTIVATE

        // LVS_EX_DOUBLEBUFFER
        // LVS_EX_TRANSPARENTBKGND ??
        if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
            SetWindowSubclass (hFilters, AlphaSubclassProcedure, 0, 0);
        }

        // TODO: list of checkboxes for FILTERS
    }

    if (auto hStatusBar = CreateWindow (STATUSCLASSNAME, L"",
                                        WS_CHILD | SBARS_SIZEGRIP | SBARS_TOOLTIPS | CCS_NOPARENTALIGN,
                                        0,0,0,0, hWnd, (HMENU) ID::STATUSBAR, cs->hInstance, NULL)) {

        // TODO: progress bar of loading (in thread) in the status bar? support badges 
        // TODO: symbols where available, text where not? try Wingdings?
        // TODO: min/max width, badge numbers draws tabcontrol (special property of tab) (leaves 3 spaces before close button?)

        SendMessage (hStatusBar, CCM_DPISCALE, TRUE, 0);
    }

    SetWindowText (hWnd, L"RADDI"); // LoadString(1) or VERSIONINFO
    // TODO: SetFocus (); !! also on activate
    OnVisualEnvironmentChange ();
    // raddi::log::event (0xA1F0, ...);
    return 0;
}

LRESULT Window::OnDestroy () {
    resolver.clear (this->hWnd);

    delete this->lists.identities;
    delete this->lists.channels;

    for (auto icon : this->icons) {
        DestroyIcon (icon);
    }
    if (IsLastWindow (hWnd)) {
        PostQuitMessage (0);
    }
    return 0;
}

namespace {
    // common finish command parameters
    //  - GUI is single threaded and synchronous thus this can be global
    //
    struct {
        std::intptr_t list = 0;
        std::intptr_t group = 0;
        std::intptr_t source = 0;
        std::vector <int> items;
        std::wstring  text;
    } finish;
}

LRESULT Window::OnCommand (UINT notification, UINT command, HWND control) {
    if ((control != NULL) && IsWindowClass (control, L"BUTTON")) {
        SetWindowLongPtr (control, GWL_STYLE, GetWindowLongPtr (control, GWL_STYLE) & ~BS_DEFPUSHBUTTON);
        SetFocus (NULL); // TODO: set focus somewhere ...active view?
    }
        
    switch (command) {

        // New Window
        case 0xC0: // Ctrl+N
            // TODO: duplicate properties of current one? except tabs
            if (auto id = database.windows.maxID.query <std::intptr_t> () + 1) {
                database.windows.insert.execute (id);

                static const auto D = CW_USEDEFAULT;
                if (auto hNewWindow = CreateWindow ((LPCTSTR) GetClassLongPtr (hWnd, GCW_ATOM), L"",
                                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, D,D,D,D, HWND_DESKTOP, NULL,
                                                    (HINSTANCE) GetWindowLongPtr (hWnd, GWLP_HINSTANCE),
                                                    (LPVOID) id)) {
                    ShowWindow (hNewWindow, SW_SHOWNORMAL);
                } else {
                    ErrorBox (hWnd, raddi::log::level::stop, 0x09);
                }
            }
            break;

        // New Tab
        case 0xC1: // Ctrl+T
        case ID::ADD_TAB_BUTTON:
            if (GetKeyState (VK_SHIFT) & 0x8000) {
                while (database.history.last [0].next ()) {
                    raddi::eid entry = database.history.last [0].get <SQLite::Blob> (0);
                    auto title = database.history.last [0].get <std::wstring> (1);
                    // auto t = database.history.last [0].get <long long> (2);

                    if (auto tabID = this->CreateTab (entry, title)) {
                        // TODO: assign 't' (timestamp)
                            
                        database.history.last [1].execute ();
                        this->tabs.views->request (tabID);
                    }
                }
            } else {
                if (auto tabID = this->CreateTab (raddi::eid ())) {
                    this->tabs.views->request (tabID);
                }
            }
            break;

        // Stack New Tab
        case 0xC2: // CTRL+Y
            if (auto tabID = this->CreateTab (raddi::eid ())) {
                if (this->tabs.views->current > 0) {
                    this->tabs.views->stack (tabID, this->tabs.views->current, true);
                }
                this->tabs.views->request (tabID);
            }
            break;
        case 0xC3: // Tab context menu
            if (auto tabID = this->CreateTab (raddi::eid ())) {
                if (this->tabs.views->contextual > 0) {
                    this->tabs.views->stack (tabID, this->tabs.views->contextual, true);
                }
                this->tabs.views->request (tabID);
            }
            break;

        // Duplicate Tab
        case 0xC4: // CTRL+K
            if (this->tabs.views->current > 0) {
                if (auto tabID = this->CreateTab (raddi::eid (), this->tabs.views->tabs [this->tabs.views->current].text)) {
                    if (!(GetKeyState (VK_SHIFT) & 0x8000)) {
                        this->tabs.views->request (tabID);
                    } else {
                        this->tabs.views->update ();
                    }
                }
            }
            break;
        case 0xC5: // Tab context menu
            if (this->tabs.views->contextual > 0) {
                if (auto tabID = this->CreateTab (raddi::eid (), this->tabs.views->tabs [this->tabs.views->contextual].text)) {
                    this->tabs.views->request (tabID);
                }
            }
            break;

        // Close Tab
        case 0xCC: // Close Whole Stack, content menu
            // TODO: prompt!!!
            this->CloseTabStack (this->tabs.views->contextual);
            break;
        case 0xCD: // Tab context menu
            this->CloseTab (this->tabs.views->contextual);
            break;
        case 0xCE: // CTRL+W / CTRL+F4
            this->CloseTab (this->tabs.views->current);
            break;

        // Quit App
        case 0xCF: // CTRL+Q
            PostMessage (NULL, WM_COMMAND, command, 0);
            break;


        // New List
        case 0xA1:
            if (this->FindFirstAvailableListId ()) {

                finish.text.clear ();
                if (EditDialogBox (hWnd, 0x60,
                                   GetDlgItem (hWnd, ID::TABS_LISTS), { metrics [SM_CXICON], metrics [SM_CYICON] },
                                   &finish.text)) {

                    if (auto new_list_id = this->FindFirstAvailableListId ()) {
                        finish.list = new_list_id;
                        finish.group = database.lists.groups.maxID.query <int> () + 1;

                        database.lists.insert (new_list_id, finish.text);
                        database.lists.groups.insert (finish.group, finish.list, LoadString (0x70));

                        FinishCommandInAllWindows (command);
                    } else {
                        MessageBox (hWnd, LoadString (0x64).c_str (), NULL, MB_ICONERROR);
                    }
                }
            } else {
                MessageBox (hWnd, LoadString (0x64).c_str (), NULL, MB_ICONERROR);
            }
            break;

        // Rename List
        case 0xA3:
            if (this->tabs.lists->contextual > 0) {

                finish.list = this->tabs.lists->contextual;
                finish.text = this->tabs.lists->tabs [this->tabs.lists->contextual].text;

                if (EditDialogBox (hWnd, 0x65,
                                   GetDlgItem (hWnd, ID::TABS_LISTS), { metrics [SM_CXICON], metrics [SM_CYICON] },
                                   &finish.text)) {

                    database.lists.rename (finish.text, finish.list);
                    FinishCommandInAllWindows (command);
                }
            }
            break;

        // Delete List
        case 0xAD:
            if (this->tabs.lists->contextual > 0) {
                if (DeleteListPrompt (hWnd, this->tabs.lists->tabs [this->tabs.lists->contextual].text)) {

                    // delete from DB
                    for (auto & query : database.lists.remove) {
                        query (this->tabs.lists->contextual);
                    }
                        
                    // notify all windows
                    finish.list = this->tabs.lists->contextual;
                    FinishCommandInAllWindows (command);
                }
            }
            break;

        // Add new List Item manually
        case 0xB0: {
            MessageBeep (0);

        } break;

        // New List Group & move selected items into it
        case 0xB1: {
            auto hList = this->tabs.lists->tabs [this->tabs.lists->current].content;

            if (EditDialogBox (hWnd, 0x72,
                               GetDlgItem (hWnd, ID::TABS_LISTS), { metrics [SM_CXICON], metrics [SM_CYICON] },
                               &finish.text)) {

                finish.list = this->tabs.lists->current;
                finish.group = database.lists.groups.maxID.query <int> () + 1;

                database.lists.groups.insert (finish.group, finish.list, finish.text);

                finish.items.clear ();
                finish.items.reserve (ListView_GetSelectedCount (hList));

                auto item = -1;
                while ((item = ListView_GetNextItem (hList, item, LVNI_SELECTED)) != -1) {
                    finish.items.push_back (item);
                    database.lists.data.move (finish.group, ListView_GetItemParam (hList, item));
                }

                database.lists.groups.cleanup (finish.list);
                FinishCommandInAllWindows (command);
            }
            SetFocus (hList);
        } break;

        // Rename List Group
        case 0xB3: {
            RECT r;
            auto hList = this->tabs.lists->tabs [this->tabs.lists->current].content;

            if (finish.group = ListView_GetFocusedGroupId (hList, &finish.text, &r)) {
                MapWindowPoints (hList, NULL, reinterpret_cast <POINT *> (&r), 2);

                if (EditDialogBox (hWnd, 0x75,
                                   NULL, { r.left + metrics [SM_CXICON], r.bottom + metrics [SM_CYICON] },
                                   &finish.text)) {

                    finish.list = this->tabs.lists->current;
                    database.lists.groups.rename (finish.text, finish.group);

                    FinishCommandInAllWindows (command);
                }
                SetFocus (hList);
            }
        } break;

        // List Item Restore Name
        case 0xB5: {
            auto hList = this->tabs.lists->tabs [this->tabs.lists->current].content;
            auto item = -1;
            while ((item = ListView_GetNextItem (hList, item, LVNI_SELECTED)) != -1) {
                database.names.removeByListedId (ListView_GetItemParam (hList, item));
            }
            FinishCommandInAllWindows (FinishCommand::RefreshList);
        } break;
            
        // Channels List Restore Name
        case 0xB6: {
            auto hList = this->tabs.lists->tabs [this->tabs.lists->current].content;
            auto item = -1;
            while ((item = ListView_GetNextItem (hList, item, LVNI_SELECTED)) != -1) {
                raddi::eid id;
                // TODO: all operations like this move to ListOfChannels class
                if (hList == this->lists.channels->hWnd) {
                    if (this->lists.channels->GetItemEid (item, &id)) {
                        database.names.remove (SQLite::Blob (id));
                    }
                }
                if (hList == this->lists.identities->hWnd) {
                    if (this->lists.identities->GetItemEid (item, &id)) {
                        database.names.remove (SQLite::Blob (id));
                    }
                }
            }
            FinishCommandInAllWindows (FinishCommand::RefreshList);
        } break;

        // Delete List Group
        case 0xBD:
            if (this->tabs.lists->current > 0) {
                const auto & tab = this->tabs.lists->tabs [this->tabs.lists->current];

                std::wstring group;
                if (auto groupID = ListView_GetFocusedGroupId (tab.content, &group)) {

                    if (DeleteListGroupPrompt (hWnd, tab.text, group)) {

                        // delete from DB
                        for (auto & query : database.lists.groups.remove) {
                            query (groupID);
                        }
                            
                        // notify all windows
                        finish.list = this->tabs.lists->current;
                        finish.group = groupID;
                        FinishCommandInAllWindows (command);
                    }
                }
            }
            break;

        // Delete List Item(s)
        case 0xBE: {
            auto hList = this->tabs.lists->tabs [this->tabs.lists->current].content;

            finish.list = this->tabs.lists->current;
            finish.items.clear ();
            finish.items.reserve (ListView_GetSelectedCount (hList));

            auto item = -1;
            while ((item = ListView_GetNextItem (hList, item, LVNI_SELECTED)) != -1) {
                finish.items.insert (finish.items.begin (), item);
            }

            if (DeleteListItemsPrompt (hWnd, finish.items.size ())) {
                for (auto item : finish.items) {
                    database.lists.data.remove (ListView_GetItemParam (hList, item));
                }
                database.lists.groups.cleanup (finish.list);
                FinishCommandInAllWindows (command);
            }
        } break;



        // Refresh channels/identities lists
        case 0xDF:
            this->lists.channels->Update ();
            this->lists.identities->Update ();
            break;


        case ID::HISTORY_BUTTON:
            // TODO: drop down menu
            MessageBeep (0);
            break;
        case ID::OVERFLOW_BUTTON:
            // TODO: enum this->tabs.views->overflow
            MessageBeep (0);
            break;
        case ID::LIST_OVERFLOW_BUTTON:
            // TODO: enum this->tabs.lists->overflow
            MessageBeep (0);
            break;

        // Rename (F2) Contextual
        case 0xF2:
            if (auto hFocus = GetFocus ()) {
                switch (auto id = GetDlgCtrlID (hFocus)) {

                    default:
                        if (id >= ID::LIST_BASE && id <= ID::LIST_LAST) {

                            if (IsWindowsVistaOrGreater () && ListView_GetFocusedGroup (hFocus) != -1) {
                                this->OnCommand (0, 0xB3, hFocus);
                            } else {
                                auto item = ListView_GetNextItem (hFocus, -1, LVNI_FOCUSED);
                                if (item != -1) {
                                    ListView_EditLabel (hFocus, item);
                                }
                            }
                        }
                        break;

                    case ID::LIST_CHANNELS:
                    case ID::LIST_IDENTITIES:
                        auto item = ListView_GetNextItem (hFocus, -1, LVNI_FOCUSED);
                        if (item != -1) {
                            ListView_EditLabel (hFocus, item);
                        }
                        break;
                }
            }
            break;

        // Show/Hide Right Pane (F4)
        case 0xF4:
            if (dividers.right > 0) {
                dividers.right_restore = dividers.right;
                dividers.right = 0;
            } else {
                if (dividers.right_restore > 0) {
                    dividers.right = dividers.right_restore;
                } else {
                    dividers.right = 256 * this->dpi / 96;
                }
            }
            if (ptrDwmExtendFrameIntoClientArea) {
                ptrDwmExtendFrameIntoClientArea (hWnd, this->GetDwmMargins ());
            }
            this->Reposition ();
            break;

        case ID::IDENTITIES:
            if (notification == CBN_SELCHANGE) {
                // TODO: remember currently selected identity
            }
            break;
    }

    // content menu command, user selected List, meaning depends...
    if (command >= ID::LIST_BASE && command <= ID::LIST_LAST) {
        auto hList = this->tabs.lists->tabs [this->tabs.lists->current].content;
        switch (Menu::LastTracked) {

            // move focused group into selected list
            case Menu::ListGroup:

                finish.source = this->tabs.lists->current;
                finish.group = ListView_GetFocusedGroupId (hList, &finish.text);
                finish.list = this->GetUserListIdFromIndex (command - ID::LIST_BASE);

                database.lists.groups.move (finish.list, finish.group);
                FinishCommandInAllWindows (command);
                break;
        }
    }

    // content menu command, user selected group within some list, meaning depends...
    if (command >= ID::LIST_SUBMENU_BASE && command <= ID::LIST_SUBMENU_LAST) {

        // always targetting specific group in a specific list
        auto hList = this->tabs.lists->tabs [this->tabs.lists->current].content;
        if (ResolveListsSubMenuItem (command, &finish.list, &finish.group)) {
            
            // if insertion to new list was requested

            if (finish.list == -1) { 
                if (this->FindFirstAvailableListId ()) {

                    finish.text.clear ();
                    if (EditDialogBox (hWnd, 0x60,
                                       GetDlgItem (hWnd, ID::TABS_LISTS), { metrics [SM_CXICON], metrics [SM_CYICON] },
                                       &finish.text)) {

                        if (auto new_list_id = this->FindFirstAvailableListId ()) {
                            finish.list = new_list_id;
                            finish.group = database.lists.groups.maxID.query <int> () + 1;

                            database.lists.insert (new_list_id, finish.text);
                            database.lists.groups.insert (finish.group, finish.list, LoadString (0x70));

                            FinishCommandInAllWindows (0xA1);
                        } else {
                            MessageBox (hWnd, LoadString (0x64).c_str (), NULL, MB_ICONERROR);
                            return 0;
                        }
                    } else
                        return 0; // cancelled
                } else {
                    MessageBox (hWnd, LoadString (0x64).c_str (), NULL, MB_ICONERROR);
                    return 0;
                }
            }

            // request to create new group

            if (finish.group == -1) {
                finish.text.clear ();
                if (EditDialogBox (hWnd, 0x72,
                                   GetDlgItem (hWnd, ID::TABS_LISTS), { metrics [SM_CXICON], metrics [SM_CYICON] },
                                   &finish.text)) {
                    finish.group = database.lists.groups.maxID.query <int> () + 1;

                    database.lists.groups.insert (finish.group, finish.list, finish.text);
                    FinishCommandInAllWindows (0xB1);
                } else
                    return 0;
            }

            // finally the action

            auto item = -1;
            switch (Menu::LastTracked) {

                case Menu::ListItem:
                case Menu::ListChannels:

                    finish.source = this->tabs.lists->current;
                    finish.items.clear ();
                    finish.items.reserve (ListView_GetSelectedCount (hList));

                    switch (Menu::LastTracked) {
                        case Menu::ListItem: // move all selected items into other list/group
                            while ((item = ListView_GetNextItem (hList, item, LVNI_SELECTED)) != -1) {
                                finish.items.insert (finish.items.begin (), item); // push front, so we can easily erase
                                database.lists.data.move (finish.group, ListView_GetItemParam (hList, item));
                            }

                            database.lists.groups.cleanup (finish.source);
                            break;

                        case Menu::ListChannels: // adding item from list of identities/channels
                            while ((item = ListView_GetNextItem (hList, item, LVNI_SELECTED)) != -1) {
                                raddi::eid eid;
                                if (hList == this->lists.channels->hWnd) {
                                    this->lists.channels->GetItemEid (item, &eid);
                                }
                                if (hList == this->lists.identities->hWnd) {
                                    this->lists.identities->GetItemEid (item, &eid);
                                }

                                database.lists.data.insert (finish.group, SQLite::Blob (eid));
                                finish.items.insert (finish.items.end (), (int) database.last_insert_rowid ());
                            }

                            // TODO: also request node to add this to subscriptions!
                            // TODO: converse with deletion
                            break;
                    }
                    FinishCommandInAllWindows (command);
                    break;
            }
        }
    }

    return 0;
}

LRESULT Window::OnFinishCommand (LPARAM command) {
    switch (command) {
        // New List
        case 0xA1:
            if (finish.list) {
                if (auto hList = Lists::Create (this, this->tabs.lists, finish.list, finish.text)) {
                    Lists::CreateGroup (hList, finish.group, LoadString (0x70));

                    this->tabs.lists->update ();
                    this->tabs.lists->move_stack (finish.list, (int) finish.list - 1);
                    this->tabs.lists->request (finish.list);

                    if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
                        SetWindowSubclass (hList, AlphaSubclassProcedure, 0, 0);
                        // SetWindowSubclass (ListView_GetHeader (hList), AlphaSubclassProcedure, 0, 0);
                    }
                    this->Reposition ();
                    this->OnVisualEnvironmentChange ();
                } else {
                    // TODO: report error
                }
            }
            break;

        // Rename List
        case 0xA3:
            this->tabs.lists->tabs [finish.list].text = finish.text;
            this->tabs.lists->update ();
            break;

        // Delete List
        case 0xAD:
            DestroyWindow (this->tabs.lists->tabs [finish.list].content);
            this->tabs.lists->tabs.erase (finish.list);
            this->tabs.lists->update ();
            break;

        // New List Group
        case 0xB1:
            if (finish.list) {
                HWND hListView = this->tabs.lists->tabs [finish.list].content;
                
                if (Lists::CreateGroup (hListView, finish.group, finish.text)) {
                    if (!finish.items.empty ()) {
                        ListView_MoveItemsToGroup (hListView, finish.group, finish.items);
                        Lists::CleanGroups (hListView);
                    }
                }
            }
            break;

        // List Group Rename
        case 0xB3:
            if (finish.list && finish.group) {
                ListView_SetGroupTitle (this->tabs.lists->tabs [finish.list].content, finish.group, finish.text);
            }
            break;

        // Delete List Group
        case 0xBD:
            if (finish.list && finish.group) {
                Lists::DeleteGroup (this->tabs.lists->tabs [finish.list].content, finish.group);
            }
            break;

        // Delete List Item
        case 0xBE:
            if (finish.list) {
                HWND hListView = this->tabs.lists->tabs [finish.list].content;
                for (auto item : finish.items) {
                    ListView_DeleteItem (hListView, item);
                }
                Lists::CleanGroups (hListView);
            }
            break;
    }

    if (command >= 0x10000) {
        switch ((FinishCommand) (command - 0x10000)) {

            case FinishCommand::RefreshList:
                InvalidateRect (this->tabs.lists->tabs [this->tabs.lists->current].content, NULL, FALSE);
                break;
        }
    }

    if (command >= ID::LIST_BASE && command <= ID::LIST_LAST) {
        switch (Menu::LastTracked) {

            case Menu::ListGroup:
                if (finish.source && finish.list && finish.group) {
                    HWND hSourceListView = this->tabs.lists->tabs [finish.source].content;
                    HWND hTargetListView = this->tabs.lists->tabs [finish.list].content;

                    if (Lists::CreateGroup (hTargetListView, finish.group, finish.text)) {
                        ListView_CopyGroupToListView (hSourceListView, finish.group, hTargetListView);
                        Lists::DeleteGroup (hSourceListView, finish.group);
                        ListView_EnableGroupView (hTargetListView, ListView_GetGroupCount (hTargetListView) > 1);
                    }
                }
                break;
        }
    }

    if (command >= ID::LIST_SUBMENU_BASE && command <= ID::LIST_SUBMENU_LAST) {
        switch (Menu::LastTracked) {

            case Menu::ListItem:
                if (finish.list) {
                    HWND hSourceListView = this->tabs.lists->tabs [finish.source].content;
                    HWND hTargetListView = this->tabs.lists->tabs [finish.list].content;

                    if (finish.list == finish.source) {
                        ListView_MoveItemsToGroup (hSourceListView, finish.group, finish.items);
                    } else {
                        LVITEM item;
                        item.mask = LVIF_GROUPID | LVIF_PARAM | LVIF_STATE | LVIF_TEXT | LVIF_IMAGE | LVIF_INDENT;
                        item.iSubItem = 0;

                        for (auto & i : finish.items) {
                            wchar_t buffer [2]; // dummy, it's LPSTR_TEXTCALLBACK actually
                            item.iItem = i;
                            item.pszText = buffer;
                            item.cchTextMax = sizeof buffer / sizeof buffer [0];

                            if (ListView_GetItem (hSourceListView, &item)) {
                                item.iGroupId = finish.group;
                                ListView_InsertItem (hTargetListView, &item);
                                ListView_DeleteItem (hSourceListView, i); // this requires finish.items in descending order!
                            }
                        }
                    }

                    Lists::CleanGroups (hSourceListView);
                    ListView_EnableGroupView (hTargetListView, ListView_GetGroupCount (hTargetListView) > 1);
                }
                break;

            case Menu::ListChannels:
                if (finish.list) {
                    HWND hTargetListView = this->tabs.lists->tabs [finish.list].content;
                    for (auto & i : finish.items) {
                        Lists::InsertEntry (hTargetListView, i, finish.group);
                    }
                }
                break;
        }
    }
    return 0;
}
 
void Window::Reposition () {
    RedrawWindow (hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
    SetWindowPos (hWnd, NULL, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    InvalidateRect (hWnd, NULL, TRUE);
}

// TODO: tabs overflow menu (move tab to front, if overflown)

LRESULT Window::OnNotify (NMHDR * nm) {
    switch (nm->idFrom) {

        case ID::TABS_VIEWS:
            switch (nm->code) {
                case TCN_UPDATED:
                    if (this->tabs.lists) {
                        if (auto h = GetDlgItem (this->tabs.views->hWnd, ID::OVERFLOW_BUTTON)) {
                            EnableWindow (h, !this->tabs.views->overflow.empty ());
                        }
                    }
                    break;
                case TCN_SELCHANGE:
                    database.current.set (this->id, "tab", this->tabs.views->current);

                    // TODO: don't set if tab changed from keyboard
                    SetFocus (this->tabs.views->tabs [this->tabs.views->current].content);
                    break;

                case RBN_LAYOUTCHANGED:
                    for (const auto & tab : this->tabs.views->tabs) {
                        if (tab.first > 0) {
                            database.tabs.update (this->id, tab.first, tab.second.stack_index, SQLite::Blob (raddi::eid ()), 0); // window, id, stack, entry, scroll
                        }
                    }
                    break;

                case NM_CLICK:
                    // TODO: if ALT, close all except this one!
                    if (reinterpret_cast <const NMMOUSE *> (nm)->dwHitInfo == HTCLOSE) {
                        this->CloseTab (reinterpret_cast <const NMMOUSE *> (nm)->dwItemSpec);
                    }
                    break;
            }
            break;

        case ID::TABS_LISTS:
            switch (nm->code) {
                case TCN_UPDATED:
                    if (this->tabs.lists) {
                        if (auto h = GetDlgItem (this->tabs.lists->hWnd, ID::LIST_OVERFLOW_BUTTON)) {
                            EnableWindow (h, !this->tabs.lists->overflow.empty ());
                        }

                        // update all menus that list Lists

                        std::vector <std::wstring> overview;
                        overview.reserve (this->tabs.lists->tabs.size ());

                        for (const auto & tab : this->tabs.lists->tabs) {
                            if (tab.first > 0) {
                                overview.push_back (tab.second.text);
                            }
                        }

                        UpdateContextMenu (Menu::ListGroup, L'\x200B', Window::ID::LIST_BASE, overview);
                    }
                    break;
                case TCN_SELCHANGE:
                    database.current.set (this->id, "list", this->tabs.lists->current);
                    break;
            }
            break;
        case ID::TABS_FEEDS:
            switch (nm->code) {
                case TCN_SELCHANGE:
                    database.current.set (this->id, "feed", this->tabs.feeds->current);
                    break;
            }
            break;

        case ID::LIST_CHANNELS:
            return this->lists.channels->OnNotify (nm);
        case ID::LIST_IDENTITIES:
            return this->lists.identities->OnNotify (nm);

        default:
            if (nm->idFrom >= ID::LIST_BASE && nm->idFrom <= ID::LIST_LAST) {
                return Lists::OnNotify (this, nm);
            }

            switch (nm->code) {
                case TTN_GETDISPINFO:
                    if (auto nmTip = reinterpret_cast <NMTTDISPINFO *> (nm)) {
                        nmTip->hinst = reinterpret_cast <HMODULE> (&__ImageBase);
                        nmTip->lpszText = MAKEINTRESOURCE (nmTip->lParam);
                    }
                    break;
            }
    }

    if (nm->code == NM_OUTOFMEMORY) {
        // TODO: same error dialog as for catching std::bad_alloc&
    }
    return 0;
}

LONG Window::UpdateStatusBar (HWND hStatusBar, UINT dpi, const RECT & rParent) {
    RECT rStatus;
    SendMessage (hStatusBar, WM_SIZE, 0, MAKELPARAM (rParent.right, rParent.bottom));
    if (GetClientRect (hStatusBar, &rStatus)) {

        static const short widths [] = { 32, 128/*, 128, 128*/ };
        static const auto  n = sizeof widths / sizeof widths [0];

        int scaled [n + 1];
        int offset = rParent.right;
        int i = n;

        while (i-- != 0) {
            offset -= dpi * widths [i] / 96;
            scaled [i] = offset;
        }

        scaled [n] = -1;
        minimum.statusbar.cx = rParent.right - scaled [0];
        minimum.statusbar.cy = rStatus.bottom;

        SendMessage (hStatusBar, SB_SETPARTS, n + 1, (LPARAM) scaled);
        return rStatus.bottom;
    } else
        return 0;
}

std::intptr_t Window::TabIdFromContentMenu (LONG * x, LONG * y, TabControlInterface * tc) {
    if ((*x == -1) && (*y == -1)) {

        auto r = tc->outline (tc->current);
        MapWindowPoints (tc->hWnd, HWND_DESKTOP, (POINT *) &r, 2);
        *x = r.left + metrics [SM_CXSMICON] / 2;
        *y = r.bottom - metrics [SM_CYSMICON] / 2;

        tc->contextual = tc->current;
    }
    return tc->contextual;
}

std::intptr_t Window::FindFirstAvailableListId () const {
    // user list tab IDs are 1...ID::MAX_LISTS; and map is ordered, so this is simple
    std::intptr_t id = 1;
    for (auto & tab : this->tabs.lists->tabs) {
        if (tab.first > 0) {
            if (tab.first != id)
                break;

            ++id;
        }
    }
    if (id <= ID::MAX_LISTS)
        return id;
    else
        return 0;
}

LRESULT Window::OnContextMenu (HWND hChild, LONG x, LONG y) {
    const auto id = GetDlgCtrlID (hChild);
    switch (id) {
        case ID::TABS_LISTS:
            if (auto tab = TabIdFromContentMenu (&x, &y, this->tabs.lists)) {
                TrackContextMenu (hWnd, x, y, Menu::ListTabs, tab);
            }
            break;

        case ID::TABS_VIEWS:
            if (auto tab = TabIdFromContentMenu (&x, &y, this->tabs.views)) {
                TrackContextMenu (hWnd, x, y, Menu::ViewTabs, tab);
            }
            break;

        case ID::LIST_IDENTITIES:
            return this->lists.identities->OnContextMenu (this, x, y);
        case ID::LIST_CHANNELS:
            return this->lists.channels->OnContextMenu (this, x, y);

        case ID::IDENTITIES:
            if ((x == -1) && (y == -1)) {
                RECT r;
                GetWindowRect (hChild, &r);
                x = r.left + metrics [SM_CXSMICON] / 2;
                y = r.bottom - metrics [SM_CYSMICON] / 2;
            }
            TrackPopupMenu (GetSubMenu (LoadMenu (GetModuleHandle (NULL), MAKEINTRESOURCE (0x40)), 0), TPM_RIGHTBUTTON, x, y, 0, hWnd, NULL);
    }
    if (id >= ID::LIST_BASE && id <= ID::LIST_LAST) {
        return Lists::OnContextMenu (this, hChild, this->GetUserListIndexById (id - ID::LIST_BASE), x, y);
    }
    return 0;
}

std::intptr_t Window::GetUserListIdFromIndex (std::size_t index) const {
    for (const auto & tab : this->tabs.lists->tabs) {
        if (tab.first > 0) {
            if (!index--)
                return tab.first;
        }
    }
    return 0;
}
std::intptr_t Window::GetUserListIndexById (std::size_t id) const {
    std::intptr_t index = 0;
    for (const auto & tab : this->tabs.lists->tabs) {
        if (tab.first > 0) {
            if (tab.first == id) {
                break;
            }
            ++index;
        }
    }
    return index;
}

LRESULT CALLBACK Window::InitialProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCCREATE:
            try {
                auto window = new Window (hWnd, reinterpret_cast <CREATESTRUCT *> (lParam));

                SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR) window);
                SetWindowLongPtr (hWnd, GWLP_WNDPROC, (LONG_PTR) &Window::Procedure);
                
                return window->Dispatch (WM_NCCREATE, wParam, lParam);
            } catch (const std::bad_alloc &) {
                return FALSE;
            }
        case WM_DESTROY:
            if (IsLastWindow (hWnd)) {
                PostQuitMessage (0);
            }
            break;
    }
    return DefWindowProc (hWnd, message, wParam, lParam);
}

LRESULT CALLBACK Window::DefaultProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL * bResultFromDWM) {
    if (ptrDwmDefWindowProc) {

        LRESULT result = 0;
        if (ptrDwmDefWindowProc (hWnd, message, wParam, lParam, &result)) {

            if (bResultFromDWM) {
                *bResultFromDWM = TRUE;
            }
            return result;
        }
    }
    if (bResultFromDWM) {
        *bResultFromDWM = FALSE;
    }
    return DefWindowProc (hWnd, message, wParam, lParam);
}

LRESULT CALLBACK Window::Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return reinterpret_cast <Window *> (GetWindowLongPtr (hWnd, GWLP_USERDATA))->Dispatch (message, wParam, lParam);
    } catch (const SQLite::InStatementException & x) {
        ErrorBox (0x06, x.what ());
        return 0;
    } catch (const std::exception & x) {
        SetWindowLongPtr (hWnd, GWLP_WNDPROC, (LONG_PTR) InitialProcedure);
        DestroyWindow (hWnd);
        ErrorBox (0x02, x.what ());
        return 0;
    }
}

void Window::FinishCommandInAllWindows (LPARAM command) const {
    EnumThreadWindows (GetCurrentThreadId (),
                       [](HWND hWnd, LPARAM command)->BOOL {
                           SendMessage (hWnd, WM_APP_FINISH_COMMAND, 0, command);
                           return TRUE;
                       }, command);
}

namespace {
    LRESULT OnSubclassPaint (HWND hWnd, HDC _hDC, RECT, DWORD_PTR dwRefData) {
        RECT rc;
        GetClientRect (hWnd, &rc);

        HDC hDC = NULL;
        HANDLE hBuffered = ptrBeginBufferedPaint (_hDC, &rc, BPBF_TOPDOWNDIB, NULL, &hDC);

        SendMessage (hWnd, WM_ERASEBKGND, (WPARAM) hDC, 0);
        SendMessage (hWnd, WM_PRINTCLIENT, (WPARAM) hDC, 0);

        InflateRect (&rc, -(int) dwRefData, -(int) dwRefData);
        ptrBufferedPaintSetAlpha (hBuffered, &rc, 0xFF);
        ptrEndBufferedPaint (hBuffered, TRUE);
        return 0;
    };

    LRESULT CALLBACK AlphaSubclassProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        switch (message) {
            case WM_MOUSEMOVE: // ????
                if (!wParam)
                    break;

            case WM_ACTIVATE:
            case WM_VSCROLL:
            case WM_HSCROLL:
            case WM_LBUTTONDOWN: // ????
            case WM_KEYDOWN: // ????
                InvalidateRect (hWnd, NULL, FALSE);
                break;

            case WM_PAINT:
                if (design.alpha) {
                    PAINTSTRUCT ps;
                    if (HDC hDC = BeginPaint (hWnd, &ps)) {
                        OnSubclassPaint (hWnd, hDC, ps.rcPaint, dwRefData);
                    }
                    EndPaint (hWnd, &ps);
                    return 0;
                } else
                    break;
        }
        return DefSubclassProc (hWnd, message, wParam, lParam);
    }

    LRESULT CALLBACK DisableWheelSubclassProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
        switch (message) {
            case WM_MOUSEWHEEL:
                return 0;
        }
        return DefSubclassProc (hWnd, message, wParam, lParam);
    }
}

