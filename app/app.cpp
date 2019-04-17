#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <intrin.h>

#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <dwmapi.h>

#include <VersionHelpers.h>

#include <stdexcept>
#include <cstdarg>
#include <list>

#ifdef USING_WINSQLITE
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif
#include <sodium.h>
#include <lzma.h>

#include "../common/log.h"
#include "../common/lock.h"
#include "../common/file.h"
#include "../common/options.h"
#include "../common/platform.h"

#include "../core/raddi.h"
#include "../core/raddi_content.h"
#include "../core/raddi_defaults.h"

#include "tabs.h"
#include "data.h"
#include "appapi.h"
#include "errorbox.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern "C" const IID IID_IImageList;
const VS_FIXEDFILEINFO * const version = GetCurrentProcessVersionInfo ();

uuid app;
Data database;

alignas (std::uint64_t) char raddi::protocol::magic [8] = "RADDI/1";

namespace {
    LPCTSTR InitializeGUI (HINSTANCE hInstance);
    void UpdateSystemThemeProperties ();
    bool InteractiveAppDataInitialization ();
    void ThreadMessageProcedure (const MSG &);
    bool CreateWindows (HINSTANCE hInstance, LPCTSTR atom, int mode);
    LRESULT CALLBACK InitialProcedure (HWND, UINT, WPARAM, LPARAM);
}

int CALLBACK wWinMain (HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    AttachConsole (ATTACH_PARENT_PROCESS);
    if (IsDebuggerPresent ()) {
        AllocConsole ();
    }
        
    raddi::log::display (L"all"); // TODO: redirect display to internal history window
    raddi::log::initialize (option (__argc, __wargv, L"log"), raddi::defaults::log_subdir, L"app", false);

    if (version == nullptr) {
        SetLastError (ERROR_FILE_CORRUPT);
        return StopBox (0x01);
    }

    raddi::log::event (0x01,
                       HIWORD (version->dwProductVersionMS),
                       LOWORD (version->dwProductVersionMS),
                       ARCHITECTURE, BUILD_TIMESTAMP);

    raddi::log::note (0x05, "sqlite3", sqlite3_libversion (), GetModuleHandle (SQLITE3_DLL_NAME) ? SQLITE3_DLL_TYPE : L"static");
    raddi::log::note (0x05, "liblzma", lzma_version_string (), GetModuleHandle (L"liblzma") ? L"dynamic" : L"static");
    raddi::log::note (0x05, "libsodium", sodium_version_string (), GetModuleHandle (L"libsodium") ? L"dynamic" : L"static");

#ifdef CRT_STATIC
    raddi::log::note (0x05, "vcruntime", CRT_STATIC, L"static");
#else
    if (auto h = GetModuleHandle (L"VCRUNTIME140")) {
        if (auto info = GetModuleVersionInfo (h)) {
            wchar_t vs [32];
            std::swprintf (vs, sizeof vs / sizeof vs [0], L"%u.%u.%u",
                           HIWORD (info->dwFileVersionMS),
                           LOWORD (info->dwFileVersionMS),
                           HIWORD (info->dwFileVersionLS));
            raddi::log::note (0x05, "vcruntime", vs, L"dynamic");
        }
    }
#endif
    raddi::log::event (0xF0, raddi::log::path);

    if (sodium_init () == -1) {
        SetLastError (ERROR_INTERNAL_ERROR);
        return StopBox (0x02);
    }
    if (!database.initialize (hInstance)) {
        return GetLastError ();
    }
    if (!InteractiveAppDataInitialization ()) {
        return GetLastError ();
    }

    if (auto atom = InitializeGUI (hInstance)) {
        UpdateSystemThemeProperties ();

        // TODO: register for restart in case of crash or windows update
        // RegisterApplicationRestart (... , 0);
        // RegisterApplicationRecoveryCallback()
        // SetUnhandledExceptionFilter -> record to file for upload on restart
        
        if (CreateWindows (hInstance, atom, nCmdShow)) {
            if (ptrChangeWindowMessageFilter) {
                ptrChangeWindowMessageFilter (WM_APP, MSGFLT_ADD);
            }

            MSG message;
            message.wParam = 0;

            HACCEL accelerators [] = {
                LoadAccelerators (hInstance, MAKEINTRESOURCE (1)),
                LoadAccelerators (hInstance, MAKEINTRESOURCE (2)),
            };

            while (GetMessage (&message, NULL, 0u, 0u)) {
                if (message.hwnd) {
                    auto root = GetAncestor (message.hwnd, GA_ROOT);
                    if (!TranslateAccelerators (root, accelerators, &message)) {
                        if (!IsDialogMessage (root, &message)) {
                            TranslateMessage (&message);
                            DispatchMessage (&message);
                        }
                    }
                } else {
                    ThreadMessageProcedure (message);
                }
            }

            database.close ();
            
            CoUninitialize ();
            return (int) message.wParam;
        }
    }

    return GetLastError ()
         ? StopBox (0)
         : ERROR_SUCCESS;
}

namespace {
    HMENU hMainTabsMenu = NULL;

    LPCTSTR InitializeGUI (HINSTANCE hInstance) {
        AppApiInitialize ();

        const INITCOMMONCONTROLSEX classes = {
            sizeof (INITCOMMONCONTROLSEX),
            ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_COOL_CLASSES |
            ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_LINK_CLASS |
            ICC_PROGRESS_CLASS
        };

        InitCommonControls ();
        if (!InitCommonControlsEx (&classes))
            return false;

        if (!InitializeTabControl (hInstance))
            return false;

        hMainTabsMenu = GetSubMenu (LoadMenu (hInstance, MAKEINTRESOURCE (1)), 0);

        switch (CoInitializeEx (NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY)) {
            case S_OK:
            case S_FALSE: // already initialized
                break;
            default:
                return false;
        }

        if (ptrBufferedPaintInit) {
            ptrBufferedPaintInit ();
        }

        SetThemeAppProperties (STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS);

        if (ptrAllowDarkModeForApp) {
            ptrAllowDarkModeForApp (true);
        }
        
        WNDCLASSEX wndclass = {
            sizeof (WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
            InitialProcedure, 0, 0, hInstance,  NULL,
            NULL, NULL, NULL, L"RADDI", NULL
        };
        return (LPCTSTR) (std::intptr_t) RegisterClassEx (&wndclass);
    }

    bool InteractiveAppDataInitialization () {
        try {
            // get app uuid
            try {
                app = database.query <SQLite::Blob> (L"SELECT `value` FROM `app` WHERE `property`='uuid'");
            } catch (const SQLite::Exception &) { // missing or wrong size
                database.execute (L"INSERT INTO `app` VALUES ('uuid',?)", SQLite::Blob (app));
                raddi::log::event (0x02, app);
            }

            // if pool is empty, fill
            //  - TODO: generate on background during first "New user" dialog

            if (database.query <long long> (L"SELECT COUNT(*) FROM `pool`") == 0) {
                auto insert = database.prepare (L"INSERT INTO `pool` VALUES (?)");

                std::uint8_t seed [crypto_sign_ed25519_SEEDBYTES];
                for (auto i = 0u; i != 108u; ++i) {
                    randombytes_buf (seed, sizeof seed);
                    insert (SQLite::Blob (seed, sizeof seed));
                }
                sodium_memzero (seed, sizeof seed);
            }

            return true;
        } catch (const SQLite::Exception & x) {
            StopBox (5, x.what ());
            return false;
        }
    }
    
    void ThreadMessageProcedure (const MSG & message) {
        switch (message.message) {

            // WM_COMMAND
            //  - commands for whole App, forwarded from active window

            case WM_COMMAND:
                switch (message.wParam) {
                    case 0xCF: // close App
                        EnumThreadWindows (GetCurrentThreadId (),
                                           [](HWND hWnd, LPARAM)->BOOL {
                                               DestroyWindow (hWnd);
                                               return TRUE;
                                           }, 0);
                        break;
                }
                break;

            case WM_APP:
                switch (message.wParam) {

                    // WM_APP 0
                    //  - notification from other instance that we should process requests

                    case 0:
                        std::vector <std::wstring> requests;

                        if (message.lParam) {
                            try {
                                requests.reserve (message.lParam);
                            } catch (const std::bad_alloc &) {
                                // ignore preallocation failure and try anyway
                            }
                        }

                        auto select = database.prepare (L"SELECT `entry` FROM `requests`");
                        auto clear = database.prepare (L"DELETE FROM `requests`");

                        if (database.begin ()) {
                            try {
                                while (select.next ()) {
                                    requests.push_back (database.tabs.query.get <std::wstring> (0));
                                }
                                clear ();
                                database.commit ();
                            } catch (...) {
                                database.rollback ();
                                requests.clear ();
                            }
                        }

                        for (const auto & entry : requests) {
                            if (EnumThreadWindows (GetCurrentThreadId (),
                                                   [](HWND hWnd, LPARAM)->BOOL {

                                                       // TODO: is eid open here, activate (and scroll) and return FALSE

                                                       return TRUE;
                                                   }, 0) == TRUE) {
                                // all returned TRUE, direct last active window to open 'entry' in new tab
                            }
                        }
                        break;
                }
                break;
        }
    }

    enum IconSize {
        SmallIconSize = 0,
        StartIconSize,
        LargeIconSize,
        ShellIconSize,
        JumboIconSize,
        IconSizesCount
    };

    LRESULT DefaultProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, BOOL * bResultFromDWM = NULL) {
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

    namespace ID {
        enum {
            ADD_TAB_BUTTON = 0x101,
            HISTORY_BUTTON = 0x109,

            TABS_VIEWS = 0x201,
            TABS_LISTS = 0x202,
            TABS_FEEDS = 0x203,

            LIST_FIRST = 0x300,
            LIST_PEOPLE = 0x3FE,
            LIST_ALL = 0x3FF,

            IDENTITIES = 0x401,
            FILTERS = 0x402,
            FEED_RECENT = 0x410,
            FEED_TWEETS = 0x411,
            FEED_FRIENDS = 0x412,
            
            // TODO: ban management controls?

            STATUSBAR = 0xF00,
        };
    }
    
    Design design;
    Cursors cursor;

    template <typename T>
    class property {
        LPARAM window;
        UINT   id;
        T      value;
    public:
        property (LPARAM window, UINT id, T value) : window (window), id (id) {
            try {
                this->value = database.property.load.query <T> (window, id);
            } catch (const SQLite::InStatementException &) {
                this->value = value;
            }
        }
        void operator = (const T & v) {
            this->value = v;
            database.property.save (this->window, this->id, this->value);
        }
        operator const T & () const { return this->value; }
        
        property & operator = (const property & other) {
            this->value = other.value;
            database.property.save (this->window, this->id, this->value);
            return *this;
        }
    private:
        property (const property &) = delete;
    };

    class Window {
        HWND hWnd;
        LPARAM id;

        HWND hToolTip = NULL;
        HICON icons [IconSizesCount] = { NULL, NULL, NULL, NULL, NULL };

        int  metrics [SM_CMETRICS];
        bool active = false;
        long extension = 0;
        long dpi = 96;
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
            struct {
                HFONT handle = NULL;
                long  height = 0;

                bool Update (HTHEME hTheme, UINT dpi, UINT dpiNULL, int id, const wchar_t * replace = nullptr, int m = 0, int d = 0) {
                    LOGFONT lf;
                    if (GetThemeSysFont (hTheme, id, &lf) == S_OK) {
                        if (replace) {
                            std::wcscpy (lf.lfFaceName, replace);
                        }
                        lf.lfHeight = MulDiv (lf.lfHeight, dpi, dpiNULL);
                        if (d != 0) {
                            lf.lfHeight = MulDiv (lf.lfHeight, m, d);
                        }
                        if (lf.lfHeight > 0) {
                            this->height = 72 * lf.lfHeight / 96;
                        } else {
                            this->height = -this->height;
                        }

                        if (auto hNewFont = CreateFontIndirect (&lf)) {
                            if (this->handle = NULL) {
                                DeleteObject (this->handle);
                            }
                            this->handle = hNewFont;
                            return true;
                        } else {
                            if (this->handle = NULL) {
                                this->handle = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
                            }
                        }
                    }
                    return false;
                }
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
        LRESULT OnGetMinMaxInfo (MINMAXINFO *);
        LRESULT OnPaint ();
        LRESULT OnControlPrePaint (HDC hDC, HWND hControl);
        LRESULT OnDpiChange (WPARAM dpi, const RECT * target);
        LRESULT OnVisualEnvironmentChange ();
        LRESULT RefreshVisualMetrics (UINT dpiNULL = GetDPI (NULL));
        
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

        static LRESULT CALLBACK Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
            try {
                return reinterpret_cast <Window *> (GetWindowLongPtr (hWnd, GWLP_USERDATA))->Dispatch (message, wParam, lParam);
            } catch (const std::exception & x) {
                SetWindowLongPtr (hWnd, GWLP_WNDPROC, (LONG_PTR) InitialProcedure);
                DestroyWindow (hWnd);
                ErrorBox (0x02, x.what ());
                return 0;
            }
        }
    public:
        explicit Window (HWND hWnd, WPARAM wParam, LPARAM lParam, CREATESTRUCT * cs)
            : hWnd (hWnd)
            , id ((LPARAM) cs->lpCreateParams)
            , dpi (GetDPI (hWnd))
            , dividers {
                { id, 0x201, long (256 * dpi / 96) }, // left
                { id, 0x202, long (256 * dpi / 96) }, // right
                { id, 0x203, 0 }, // right_restore
                //{ id, 0x101, 0 }, // accounts
                //{ id, 0x102, 0 }, // filters
                { id, 0x103, double (256 * dpi / 96) }, // feeds
              } {

            std::memset (this->metrics, 0, sizeof this->metrics);

            SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR) this);
            SetWindowLongPtr (hWnd, GWLP_WNDPROC, (LONG_PTR) &Window::Procedure);
            this->Dispatch (WM_NCCREATE, wParam, lParam);
        };
    };

    bool CreateWindows (HINSTANCE hInstance, LPCTSTR atom, int nCmdShow) {
        bool any = false;
        auto n = database.query <long long> (L"SELECT COUNT(*) FROM `windows`");
        auto select = database.prepare (L"SELECT `id`,`x`,`y`,`w`,`h`,`m`"
                                        L" FROM `windows`"
                                        L" ORDER BY `m`=2 DESC, `m`=3 DESC, `m`=1 DESC");
        if (select.next ()) {
            do {
                auto id = select.get <std::intptr_t> (0);
                RECT r = FixWindowCoordinates (select.get <int> (1), select.get <int> (2),
                                               select.get <int> (3), select.get <int> (4));

                if (auto hWnd = CreateWindow (atom, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                              r.left, r.top, r.right, r.bottom,
                                              HWND_DESKTOP, NULL, hInstance, (LPVOID) id)) {
                    auto mode = select.get <int> (5);
                    if (select.row == n) {
                        if (mode == SW_SHOWMINIMIZED) mode = SW_SHOWNORMAL;
                        if (nCmdShow != SW_SHOWDEFAULT) mode = nCmdShow;
                    }
                    ShowWindow (hWnd, mode);
                    any = true;
                } else {
                    StopBox (0x09);
                }
            } while (select.next ());
        } else {
            static const auto D = CW_USEDEFAULT;
            if (auto hWnd = CreateWindow (atom, L"", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                          D,D,D,D, HWND_DESKTOP, NULL, hInstance, (LPVOID) 1)) {
                ShowWindow (hWnd, nCmdShow);
                any = true;
            } else {
                StopBox (0x09);
            }
        }
        return any;
    }

    LRESULT CALLBACK InitialProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_NCCREATE:
                try {
                    new Window (hWnd, wParam, lParam, reinterpret_cast <CREATESTRUCT *> (lParam));
                    break;
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
                delete this;
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

                    int action = IDYES;
                    if (ptrTaskDialogIndirect) {
                        TASKDIALOG_BUTTON dlgCloseWindowButtons [] = {
                            { IDYES, L"Stash\nReopen from menu and whatever" },
                            { IDNO, L"Close\nAll tabs and window begone" },
                        };

                        TASKDIALOGCONFIG dlgCloseWindow;
                        std::memset (&dlgCloseWindow, 0, sizeof dlgCloseWindow);

                        dlgCloseWindow.cbSize = sizeof dlgCloseWindow;
                        dlgCloseWindow.hwndParent = hWnd;
                        dlgCloseWindow.hInstance = reinterpret_cast <HMODULE> (&__ImageBase);
                        dlgCloseWindow.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
                        dlgCloseWindow.dwCommonButtons = TDCBF_CANCEL_BUTTON;
                        dlgCloseWindow.pszWindowTitle = L"close window TBD TBD TBD TBD TBD TBD ";
                        dlgCloseWindow.pszMainIcon = TD_WARNING_ICON;
                        dlgCloseWindow.pszContent = L"text text text text text text text";
                        dlgCloseWindow.cButtons = sizeof dlgCloseWindowButtons / sizeof dlgCloseWindowButtons [0];
                        dlgCloseWindow.pButtons = dlgCloseWindowButtons;
                        dlgCloseWindow.nDefaultButton = IDYES;
                        dlgCloseWindow.pszVerificationText = L"TBD: Remember (revert in settings)";

                        BOOL remember = FALSE;
                        if (ptrTaskDialogIndirect (&dlgCloseWindow, &action, NULL, &remember) != S_OK) {
                            // error?
                        }
                        if (remember) {
                            // save
                        }
                    } else {
                        action = MessageBox (hWnd, L"TBD: Stash???", L"Close Window", MB_YESNOCANCEL | MB_ICONQUESTION);
                    }

                    switch (action) {
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
                if (!IsIconic (hWnd)) {
                    if (auto rWindow = (RECT *) lParam) {
                        rWindow->left += metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
                        rWindow->right -= metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
                        rWindow->bottom -= metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];
                    }
                    return 0;
                } else
                    break;
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
                UpdateSystemThemeProperties ();
                OnVisualEnvironmentChange ();
                InvalidateRect (hWnd, NULL, FALSE);
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
                        // APPCOMMAND_SAVE, NEW, OPEN, CLOSE
                }
                break;
        }
        return DefaultProcedure (hWnd, message, wParam, lParam);
    }

    LRESULT OnSubclassPaint (HWND hWnd, HDC _hDC, RECT, DWORD_PTR dwRefData) {
        RECT rc;
        GetClientRect (hWnd, &rc);

        // paint to black bg, paint to white bg
        //  - evaluate source alpha by trying all color/alphas (binary halving) and evaluating results
        //  - then set premultiplied
        //
        // SRC    A     input1 tg1   input2 tg2
        // 0x00 0x00 -> 0x00 = 0x00, 0xFF = 0xFF
        // 0xFF 0x00 -> 0x00 = 0x00, 0xFF = 0xFF
        // 0x00 0xFF -> 0x00 = 0x00, 0xFF = 0x00
        // 0xFF 0xFF -> 0x00 = 0xFF, 0xFF = 0xFF
        // 

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
            //case WM_MOUSEMOVE: // ????
            //    if (!wParam)
            //        break;
            /*case WM_VSCROLL:
            case WM_HSCROLL:
            case WM_LBUTTONDOWN: // ????
            case WM_KEYDOWN: // ????
                InvalidateRect (hWnd, NULL, FALSE);
                break;// */

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

    bool IsWindowsVista () {
        return IsWindowsVistaOrGreater ()
            && !IsWindows7OrGreater ();
    }
    bool IsWindows7 () {
        return IsWindows7OrGreater ()
            && !IsWindows8OrGreater ();
    }

    bool Window::GetCaptionTextColor (COLORREF & color, UINT & glow) const {
        glow = 2 * this->metrics [SM_CXFRAME] / 2;
        color = GetSysColor (COLOR_WINDOWTEXT);
        
        bool dark = !design.light
                 || (IsWindowsVista () && IsZoomed (this->hWnd))
                 || (!IsWindows7 () && design.prevalence && IsColorDark (design.colorization.active));

        if (this->active) {
            if (dark) {
                color = 0xFFFFFF;
                if (IsWindowsVista ()) {
                    glow = 0;
                }
            }
        } else {
            if (!IsWindows7 ()) {
                color = GetSysColor (COLOR_GRAYTEXT);
            }
        }
        return dark;
    }

    std::intptr_t Window::CreateTab (const raddi::eid & entry, const std::wstring & text, std::intptr_t id) {
        if (id == 0) {
            id = this->tabs.views->tabs.crbegin ()->first + 1;
            if (id < 1) {
                id = 1;
            }
            // TODO: schedule repositioning
        }

        try {
            wchar_t szTmp [16];
            _snwprintf (szTmp, 16, L"[%zu] ", id);

            this->tabs.views->tabs [id].text = szTmp + text;
            this->tabs.views->tabs [id].icon = NULL; // TODO: loading animating ICO and EID in title

            this->tabs.views->tabs [id].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", szTmp,
                                                                  WS_CHILD | WS_CLIPSIBLINGS,// | WS_BORDER,// | WS_HSCROLL | WS_VSCROLL,
                                                                  0, 0, 0, 0, this->hWnd, NULL,
                                                                  (HINSTANCE) GetWindowLongPtr (this->hWnd, GWLP_HINSTANCE), NULL);

            // this->tabs.views->tabs [id].progress = 1; // TODO: enqueue threadpool item to load the
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

            tc->stacking = true;
            tc->badges = true;

            tc->tabs [-4].text = L"\xE128"; // "\x2081\x2082"; // NET
            tc->tabs [-4].tip = raddi::log::translate (raddi::log::rsrc_string (0x11), std::wstring ());
            tc->tabs [-4].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"NET",
                                                    WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, NULL,
                                                    cs->hInstance, NULL);
            tc->tabs [-3].text = L"\xE1CF"; // FAVorites
            tc->tabs [-3].tip = raddi::log::translate (raddi::log::rsrc_string (0x12), std::wstring ());
            tc->tabs [-3].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"FAV",
                                                    WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, NULL,
                                                    cs->hInstance, NULL);
            tc->tabs [-2].text = L"\xE142"; // "\x2085\x2086"; // NOTification
            tc->tabs [-2].tip = raddi::log::translate (raddi::log::rsrc_string (0x13), std::wstring ());
            tc->tabs [-2].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"NOT",
                                                    WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, NULL,
                                                    cs->hInstance, NULL);
            tc->tabs [-1].text = L"\xE205"; // "\x2089\x208A";// PRIvate messages
            tc->tabs [-1].tip = raddi::log::translate (raddi::log::rsrc_string (0x14), std::wstring ());
            tc->tabs [-1].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"PRIV MSG",
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

            try {
                while (database.lists.query.next ()) {
                    auto id = database.lists.query.get <std::size_t> (0);
                    auto & tab = tc->tabs [id];

                    tab.text = database.lists.query.get <std::wstring> (1);
                    tab.content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"",
                                                  WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE |// WS_BORDER |
                                                  LVS_LIST | LVS_EDITLABELS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS,
                                                  0, 0, 0, 0, hWnd, (HMENU) (ID::LIST_FIRST + id), cs->hInstance, NULL);
                    // LVS_EX_DOUBLEBUFFER | LVS_EX_ONECLICKACTIVATE
                    // LVS_EX_TRANSPARENTBKGND ??

                    tc->update ();
                }
            } catch (const SQLite::Exception & x) {
                ErrorBox (hWnd, raddi::log::level::error, 0x04, x.what ());
            }

            tc->tabs [-7].text = L"Channels";
            tc->update ();
            tc->tabs [-3].text = L"People"; // TODO: use images here also
            tc->update ();

            tc->tabs [-7].content =
                CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"",
                                WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE |
                                LVS_LIST | LVS_EDITLABELS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS,
                                0, 0, 0, 0, hWnd, (HMENU) ID::LIST_ALL, cs->hInstance, NULL);
            tc->tabs [-3].content =
                CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"",
                                WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE |
                                LVS_LIST | LVS_EDITLABELS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS,
                                0, 0, 0, 0, hWnd, (HMENU) ID::LIST_PEOPLE, cs->hInstance, NULL);

            if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
                for (auto & tab : tc->tabs) {
                    SetWindowSubclass (tab.second.content, AlphaSubclassProcedure, 0, 0);
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
            tc->addbutton (0xF4, IsWindowsVistaOrGreater () ? L"\x25B6" : L">>", 0x30, true);

            // tc->badges = true; // ???
            tc->tabs [1].text = L"Activity";
            tc->tabs [1].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"ACTIVITY",
                                                   WS_CHILD | WS_CLIPSIBLINGS, 0,0,0,0, hWnd, (HMENU) ID::FEED_RECENT,
                                                   cs->hInstance, NULL);
            tc->tabs [2].text = L"Followed";
            tc->tabs [2].content = CreateWindowEx (WS_EX_NOPARENTNOTIFY, L"STATIC", L"FRIENDS",
                                                   WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hWnd, (HMENU) ID::FEED_FRIENDS,
                                                   cs->hInstance, NULL);
            tc->tabs [3].text = L"Tweets";
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
            SendDlgItemMessage (hWnd, ID::IDENTITIES, CB_ADDSTRING, 0, (LPARAM) L"TEST TEST TEST");
            SendDlgItemMessage (hWnd, ID::IDENTITIES, CB_ADDSTRING, 0, (LPARAM) L"AAA");
            SendDlgItemMessage (hWnd, ID::IDENTITIES, CB_ADDSTRING, 0, (LPARAM) L"BBBBB");
            if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
                SetWindowSubclass (hIdentities, AlphaSubclassProcedure, 0, 2);
            }
        }

        // TODO: "manage" buttons aside of combobox
        // TODO: identities: disable mouse wheel!!! subclass

        if (auto hFilters = CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"",
                                            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE |// WS_BORDER |
                                            LVS_LIST | LVS_EDITLABELS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS,
                                            0, 0, 0, 0, hWnd, (HMENU) ID::FILTERS, cs->hInstance, NULL)) {

            if (IsWindowsVistaOrGreater () && !IsWindows10OrGreater ()) {
                SetWindowSubclass (hFilters, AlphaSubclassProcedure, 0, 0);
            }
        }
        // LVS_EX_DOUBLEBUFFER | LVS_EX_ONECLICKACTIVATE
        // LVS_EX_TRANSPARENTBKGND ??

        // TODO: list of checkboxes for FILTERS

        CreateWindow (STATUSCLASSNAME, L"",
                      WS_CHILD | SBARS_SIZEGRIP | SBARS_TOOLTIPS | CCS_NOPARENTALIGN,
                      0,0,0,0, hWnd, (HMENU) ID::STATUSBAR, cs->hInstance, NULL);

        // TODO: progress bar of loading (in thread) in the status bar? support badges 
        // TODO: symbols where available, text where not? try Wingdings?
        // TODO: min/max width, badge numbers draws tabcontrol (special property of tab) (leaves 3 spaces before close button?)
        
        SendDlgItemMessage (hWnd, ID::STATUSBAR, CCM_DPISCALE, TRUE, 0);
        SetWindowText (hWnd, L"RADDI"); // LoadString(1) or VERSIONINFO
        // TODO: SetFocus (); !! also on activate
        OnVisualEnvironmentChange ();
        // raddi::log::event (0xA1F0, ...);
        return 0;
    }

    LRESULT Window::OnDestroy () {
        DeleteObject (this->fonts.tabs.handle);
        DeleteObject (this->fonts.tiny.handle);
        for (auto icon : this->icons) {
            DestroyIcon (icon);
        }
        if (IsLastWindow (hWnd)) {
            PostQuitMessage (0);
        }
        return 0;
    }

    LRESULT Window::OnCommand (UINT notification, UINT id, HWND control) {
        if (IsWindowClass (control, L"BUTTON")) {
            SetWindowLongPtr (control, GWL_STYLE, GetWindowLongPtr (control, GWL_STYLE) & ~BS_DEFPUSHBUTTON);
            SetFocus (NULL); // TODO: set focus somewhere
        }
        
        switch (id) {

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
                            // TODO: assign 't'
                            
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
            case 0xCD: // Tab context menu
                this->CloseTab (this->tabs.views->contextual);
                break;
            case 0xCE: // CTRL+W / CTRL+F4
                this->CloseTab (this->tabs.views->current);
                break;

            // Quit App
            case 0xCF: // CTRL+Q
                PostMessage (NULL, WM_COMMAND, id, 0);
                break;

            case ID::HISTORY_BUTTON:
                // TODO: drop down menu
                MessageBeep (0);
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
                RedrawWindow (hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
                SetWindowPos (hWnd, NULL, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                InvalidateRect (hWnd, NULL, TRUE);
                break;
        }
        return 0;
    }

    // TODO: tabs overflow menu (move tab to front, if overflown)

    LRESULT Window::OnNotify (NMHDR * nm) {
        switch (nm->idFrom) {

            case ID::TABS_VIEWS:
                switch (nm->code) {
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

            /*default:
                switch (nm->code) {
                    case TTN_GETDISPINFO:
                        if (auto nmTip = reinterpret_cast <NMTTDISPINFO *> (nm)) {
                            nmTip->hinst = reinterpret_cast <HMODULE> (&__ImageBase);
                            nmTip->lpszText = MAKEINTRESOURCE (nmTip->lParam);
                        }
                        break;
                }*/
        }
        return 0;
    }

    const MARGINS * Window::GetDwmMargins () {
        static MARGINS margins = { 0,0,0,0 };
        margins.cyTopHeight = this->extension + metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];

        if (!IsWindows10OrGreater ()) {
            if (design.nice) {
                margins.cxLeftWidth = dividers.left;
                margins.cxRightWidth = dividers.right;
                margins.cyBottomHeight = metrics [SM_CYCAPTION];
            }
        }
        return &margins;
    }

    LRESULT Window::OnGetMinMaxInfo (MINMAXINFO * mmi) {
        if (minimum.statusbar.cx || this->tabs.views->minimum.cx) {
            mmi->ptMinTrackSize.x = std::max (minimum.statusbar.cx,
                                              this->tabs.views->minimum.cx + dividers.left + dividers.right)
                                  + 2 * (metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER]);
        }
        mmi->ptMinTrackSize.y = this->extension
                              + 2 * (metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER])
                              + minimum.statusbar.cy
                              + this->tabs.feeds->minimum.cy
                              + 6 * this->fonts.text.height;
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

    void DeferWindowPos (HDWP & hDwp, HWND hCtrl, const RECT & r, UINT flags = 0) {
        hDwp = DeferWindowPos (hDwp, hCtrl, NULL, r.left, r.top, r.right, r.bottom, SWP_NOACTIVATE | SWP_NOZORDER | flags);
    }

    void Window::UpdateListsPosition (HDWP & hDwp, const RECT & client, const RECT & rListTabs) {
        auto r = this->GetTabControlContentRect (this->GetListsFrame (&client, rListTabs));
        r.right -= r.left;
        r.bottom -= r.top;

        for (const auto & tab : this->tabs.lists->tabs) {
            if (tab.second.content)
                DeferWindowPos (hDwp, tab.second.content, r, 0);
        }
    }
    void Window::UpdateViewsPosition (HDWP & hDwp, const RECT & client) {
        auto r = this->GetTabControlContentRect (this->GetViewsFrame (&client));
        if (design.composited && !IsWindows10OrGreater ()) {
            r.right += 2;
            r.bottom += metrics [SM_CYFRAME] + 3;
        }
        r.right -= r.left;
        r.bottom -= r.top;

        for (const auto & tab : this->tabs.views->tabs) {
            if (tab.second.content)
                DeferWindowPos (hDwp, tab.second.content, r, 0);
        }
    }
    void Window::UpdateFeedsPosition (HDWP & hDwp, const RECT & client, const RECT & rFeedsTabs) {
        auto r = this->GetTabControlContentRect (this->GetFeedsFrame (&client, rFeedsTabs));
        r.right -= r.left;
        r.bottom -= r.top;

        for (const auto & tab : this->tabs.feeds->tabs) {
            if (tab.second.content)
                DeferWindowPos (hDwp, tab.second.content, r, 0);
        }
    }

    RECT Window::GetListsTabRect () {
        RECT r;
        r.top = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + this->tabs.views->minimum.cy;
        r.left = 0;

        if (IsAppThemed ()) {
            if (!IsZoomed (hWnd)) {
                if (!design.contrast) {
                    r.left = metrics [SM_CXPADDEDBORDER];
                }
                if (!IsWindowsVistaOrGreater ()) {
                    r.left = metrics [SM_CXFRAME];
                }
            }
        } else {
            r.left = 2;
        }

        r.right = dividers.left - r.left - (metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER]);
        r.bottom = this->tabs.lists->minimum.cy + 1;
        return r;
    }

    RECT Window::GetFeedsTabRect (const RECT & rRightFrame) {
        RECT r;
        r.top = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + LONG (dividers.feeds);
        r.left = rRightFrame.left;
        r.right = rRightFrame.right;
        r.bottom = this->tabs.feeds->minimum.cy;

        if (!IsAppThemed ()) {
            r.left += 2;
            r.right -= 2;
        }
        return r;
    }

    LRESULT Window::OnPositionChange (const WINDOWPOS & position) {
        if (!(position.flags & SWP_NOSIZE) || (position.flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED))) {
            if (this->tabs.views && this->tabs.lists && this->tabs.feeds) {

                RECT client;
                if (GetClientRect (hWnd, &client)) {

                    if (this->height) {
                        this->dividers.feeds = (double) (this->dividers.feeds) * double (client.bottom) / double (this->height);
                    }
                    this->height = client.bottom;

                    RECT tabs = {
                        dividers.left - 1,
                        metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + 1,
                        0,
                        this->tabs.views->minimum.cy
                    };

                    RECT buttons;
                    if (design.composited
                        && ptrDwmGetWindowAttribute
                        && ptrDwmGetWindowAttribute (hWnd, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof buttons) == S_OK) {

                        tabs.right = buttons.left - dividers.left - metrics [SM_CXFRAME];
                    } else {
                        tabs.right = client.right - 3 * metrics [SM_CXSIZE] - dividers.left - metrics [SM_CXFRAME];
                    }
                    tabs.right = std::min (tabs.right, client.right - dividers.right - tabs.left - metrics [SM_CXFRAME]);
                    if (!IsAppThemed ()) {
                        tabs.left += 2;
                        tabs.right -= 2;
                    }

                    auto hStatusBar = GetDlgItem (hWnd, ID::STATUSBAR);
                    auto yStatusBar = UpdateStatusBar (hStatusBar, dpi, client);
                    if (design.nice) {
                        client.bottom -= metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
                        yStatusBar = 0;
                    }

                    if (HDWP hDwp = BeginDeferWindowPos (16 + this->tabs.views->tabs.size () + this->tabs.lists->tabs.size ())) {
                        auto rListTabs = this->GetListsTabRect ();
                        auto rRightPane = this->GetRightPane (client, rListTabs);
                        auto rFeedsTabs = this->GetFeedsTabRect (rRightPane);
                        auto rFilters = this->GetFiltersRect (&client, rRightPane);

                        InflateRect (&rFilters, -2, -2);
                        rFilters.right = rFilters.right - rFilters.left -2;
                        rFilters.bottom = rFilters.bottom - rFilters.top;// -2;

                        DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::TABS_VIEWS), tabs);
                        DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::TABS_LISTS), rListTabs);
                        DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::TABS_FEEDS), rFeedsTabs);

                        if (yStatusBar) {
                            DeferWindowPos (hDwp, hStatusBar, { 0, client.bottom - yStatusBar, client.right, yStatusBar }, SWP_SHOWWINDOW);
                        } else {
                            DeferWindowPos (hDwp, hStatusBar, { 0,0,0,0 }, SWP_HIDEWINDOW);
                        }

                        this->UpdateViewsPosition (hDwp, client);
                        this->UpdateListsPosition (hDwp, client, rListTabs);
                        this->UpdateFeedsPosition (hDwp, client, rFeedsTabs);

                        DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::IDENTITIES), { rRightPane.left, rRightPane.top, rRightPane.right, rRightPane.bottom });
                        DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::FILTERS), rFilters);
                        
                        EndDeferWindowPos (hDwp);
                    }

                    if (this->extension != this->tabs.views->minimum.cy + 1) {
                        this->extension = this->tabs.views->minimum.cy + 1;

                        if (ptrDwmExtendFrameIntoClientArea) {
                            ptrDwmExtendFrameIntoClientArea (hWnd, GetDwmMargins ());
                        }
                        InvalidateRect (hWnd, NULL, TRUE);
                    }
                }
            }

            if (!design.composited) {
                InvalidateRect (this->tabs.views->hWnd, NULL, TRUE);
                EnumChildWindows (this->tabs.views->hWnd,
                                  [](HWND hCtrl, LPARAM)->BOOL {
                                      InvalidateRect (hCtrl, NULL, TRUE);
                                      return TRUE;
                                  }, 0);
            }
        }

        WINDOWPLACEMENT placement;
        placement.length = sizeof placement;
        GetWindowPlacement (hWnd, &placement);

        database.windows.update (placement.rcNormalPosition.left, placement.rcNormalPosition.top,
                                 placement.rcNormalPosition.right - placement.rcNormalPosition.left,
                                 placement.rcNormalPosition.bottom - placement.rcNormalPosition.top,
                                 placement.showCmd, this->id);
        return 0;
    }

    LRESULT Window::OnDpiChange (WPARAM dpi, const RECT * r) {
        dpi = LOWORD (dpi);
        if (this->dpi != dpi) {
            dividers.left = dividers.left * dpi / this->dpi;
            dividers.right = dividers.right * dpi / this->dpi;
            this->dpi = dpi;
        }

        OnVisualEnvironmentChange ();
        SetWindowPos (hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, 0);
        return 0;
    }

    LRESULT Window::RefreshVisualMetrics (UINT dpiNULL) {
        if (ptrGetSystemMetricsForDpi) {
            for (auto i = 0; i != sizeof metrics / sizeof metrics [0]; ++i) {
                this->metrics [i] = ptrGetSystemMetricsForDpi (i, this->dpi);
            }
        } else {
            for (auto i = 0; i != sizeof metrics / sizeof metrics [0]; ++i) {
                this->metrics [i] = this->dpi * GetSystemMetrics (i) / dpiNULL;
            }
        }
        return 0;
    }

    SIZE Window::GetIconMetrics (IconSize size, UINT dpiNULL) {
        switch (size) {
            case SmallIconSize:
                return { metrics [SM_CXSMICON], metrics [SM_CYSMICON] };
            case StartIconSize:
                return {
                    (metrics [SM_CXICON] + metrics [SM_CXSMICON]) / 2,
                    (metrics [SM_CYICON] + metrics [SM_CYSMICON]) / 2
                };
            case LargeIconSize:
            default:
                return { metrics [SM_CXICON], metrics [SM_CYICON] };

            case ShellIconSize:
            case JumboIconSize:
                if (IsWindowsVistaOrGreater () || (size == ShellIconSize)) { // XP doesn't have Jumbo
                    if (HMODULE hShell32 = GetModuleHandle (L"SHELL32")) {
                        HRESULT (WINAPI * ptrSHGetImageList) (int, const GUID &, void**) = NULL;

                        if (IsWindowsVistaOrGreater ()) {
                            Symbol (hShell32, ptrSHGetImageList, "SHGetImageList");
                        } else {
                            Symbol (hShell32, ptrSHGetImageList, reinterpret_cast <const char *> (727));
                        }
                        if (ptrSHGetImageList) {
                            HIMAGELIST list;
                            if (ptrSHGetImageList ((size == JumboIconSize) ? SHIL_JUMBO : SHIL_EXTRALARGE,
                                                   IID_IImageList, (void **) &list) == S_OK) {
                                int cx, cy;
                                if (ImageList_GetIconSize (list, &cx, &cy)) {
                                    switch (size) {
                                        case ShellIconSize: return { long (cx * dpi / dpiNULL), long (cy * dpi / dpiNULL) };
                                        case JumboIconSize: return { long (cx * dpi / 96), long (cy * dpi / 96) };
                                    }
                                }
                            }
                        }
                    }
                }
                switch (size) {
                    default:
                    case ShellIconSize: return { long (48 * dpi / dpiNULL), long (48 * dpi / dpiNULL) };
                    case JumboIconSize: return { long (256 * dpi / 96), long (256 * dpi / 96) };
                }
        }
    }

    void UpdateSystemThemeProperties () {
        static auto lastUpdate = 0u;
        auto now = GetTickCount ();
        if (lastUpdate != now) {
            lastUpdate = now;
        } else
            return;

        cursor.update ();
        design.update ();
    }

    BOOL WINAPI UpdateWindowTreeTheme (HWND hCtrl, LPARAM param) {
        EnumChildWindows (hCtrl, UpdateWindowTreeTheme, param);
        SetWindowTheme (hCtrl, design.light ? NULL : L"DarkMode_Explorer", NULL);
        return TRUE;
    }

    LRESULT Window::OnVisualEnvironmentChange () {
        this->dpi = GetDPI (this->hWnd);
        auto dpiNULL = GetDPI (NULL);
        auto hTheme = OpenThemeData (hWnd, L"TEXTSTYLE");

        this->fonts.text.Update (hTheme, dpi, dpiNULL, TMT_MSGBOXFONT);
        this->fonts.tabs.Update (hTheme, dpi, dpiNULL, TMT_CAPTIONFONT);
        this->fonts.tiny.Update (hTheme, dpi, dpiNULL, TMT_MENUFONT, L"Calibri", 7, 8);
        // TODO: Wingdings for some

        if (hTheme) {
            CloseThemeData (hTheme);
        }
        
        if (ptrAllowDarkModeForWindow) {
            ptrAllowDarkModeForWindow (hWnd, true);

            LONG v = !design.light;
            ptrDwmSetWindowAttribute (hWnd, 0x13, &v, sizeof v);
        }

        // SetProp (hWnd, L"UseImmersiveDarkModeColors", (HANDLE) !design.light); // ???
        EnumChildWindows (this->hWnd, UpdateWindowTreeTheme, 0);
        SetWindowTheme (this->hToolTip, design.light ? NULL : L"DarkMode_Explorer", NULL);

        SendDlgItemMessage (hWnd, ID::TABS_VIEWS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);
        SendDlgItemMessage (hWnd, ID::TABS_FEEDS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);
        SendDlgItemMessage (hWnd, ID::TABS_LISTS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);

        EnumChildWindows (this->tabs.views->hWnd,
                          [](HWND hCtrl, LPARAM font)->BOOL {
                              SendMessage (hCtrl, WM_SETFONT, (WPARAM) font, 1);
                              return TRUE;
                          }, (LPARAM) this->fonts.tabs.handle);

        SendMessage (this->hToolTip, WM_SETFONT, (WPARAM) this->fonts.text.handle, 0);
        SendDlgItemMessage (hWnd, ID::IDENTITIES, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);
        SendDlgItemMessage (hWnd, ID::FILTERS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);

        for (const auto & tab : this->tabs.lists->tabs) {
            SendMessage (tab.second.content, WM_SETFONT, (WPARAM) this->fonts.text.handle, 0);
        }
        for (const auto & tab : this->tabs.feeds->tabs) {
            SendMessage (tab.second.content, WM_SETFONT, (WPARAM) this->fonts.text.handle, 0);
        }

        this->RefreshVisualMetrics (dpiNULL);

        for (auto i = 0u; i != IconSizesCount; ++i) {
            if (auto icon = LoadBestIcon (reinterpret_cast <HINSTANCE> (&__ImageBase), MAKEINTRESOURCE (1),
                                          GetIconMetrics ((IconSize) i, dpiNULL))) {
                if (this->icons [i]) {
                    DestroyIcon (this->icons [i]);
                }
                this->icons [i] = icon;
            }
        }
        // TODO: for XP with blue luna theme or dark colored active caption, load white with black outline
        SendMessage (hWnd, WM_SETICON, ICON_SMALL, (LPARAM) this->icons [SmallIconSize]);
        SendMessage (hWnd, WM_SETICON, ICON_BIG, (LPARAM) this->icons [IsWindows10OrGreater () ? StartIconSize : LargeIconSize]);

            // tc->tabs [301].icon = icons [SmallIconSize];
        this->tabs.views->min_tab_width = 2 * (std::uint16_t) GetIconMetrics (SmallIconSize, dpiNULL).cx;
        this->tabs.views->max_tab_width = 5 * this->tabs.views->min_tab_width; // TODO: settings

        this->tabs.views->dpi = (std::uint16_t) dpi;
        this->tabs.lists->dpi = (std::uint16_t) dpi;
        this->tabs.feeds->dpi = (std::uint16_t) dpi;

        SetWindowPos (hWnd, NULL, 0,0,0,0, SWP_FRAMECHANGED | SWP_DRAWFRAME | SWP_NOREPOSITION | SWP_NOSIZE | SWP_NOMOVE);
        return 0;
    }

    LRESULT Window::OnNonClientActivate (UINT message, WPARAM wParam, LPARAM lParam) {
        this->active = wParam;
        auto rv = DefaultProcedure (hWnd, message, wParam, lParam);

        UINT redraw = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_ERASENOW | RDW_UPDATENOW;
        if (wParam) {
            redraw |= RDW_FRAME;
        }
        RedrawWindow (hWnd, NULL, NULL, redraw);
        return rv;
    }
        
    LRESULT Window::OnNonClientHitTest (UINT message, WPARAM wParam, LPARAM lParam) {
        BOOL fromDWM = FALSE;
        auto result = DefaultProcedure (hWnd, message, wParam, lParam, &fromDWM);
        POINT pt = { (signed short) LOWORD (lParam), (signed short) HIWORD (lParam) };

        switch (result) {
            case HTCLOSE:
            case HTMAXBUTTON:
            case HTMINBUTTON:
                if (design.composited && !fromDWM) {
                    result = HTCLIENT;
                }
                break;
                
            case HTCLIENT:
            case HTBORDER:
                RECT r;
                GetClientRect (hWnd, &r);
                ScreenToClient (hWnd, &pt);

                if (pt.y <= metrics [SM_CYFRAME] + metrics [SM_CYBORDER]) {
                    result = HTTOP;
                } else
                if (pt.y < this->extension + metrics [SM_CYFRAME]) {

                    if (pt.y < (metrics [SM_CYFRAME] + metrics [SM_CYCAPTION])
                     && pt.x < (metrics [SM_CXFRAME] + metrics [SM_CXSMICON])) {

                        result = HTSYSMENU;
                    } else
                    if (!design.composited && pt.y < metrics [SM_CYCAPTION] + metrics [SM_CYFRAME]) {

                        if (pt.x > r.right - 1 * metrics [SM_CXSIZE])
                            result = HTCLOSE;
                        else
                        if (pt.x > r.right - 2 * metrics [SM_CXSIZE])
                            result = HTMAXBUTTON;
                        else
                        if (pt.x > r.right - 3 * metrics [SM_CXSIZE])
                            result = HTMINBUTTON;
                        else
                            result = HTCAPTION;
                    } else {
                        result = HTCAPTION;
                    }
                } else
                if (pt.x > (r.right - (metrics [SM_CXSMICON] + metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER]))
                 && pt.y > (r.bottom - (metrics [SM_CYSMICON] + metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER]))) {
                    result = HTBOTTOMRIGHT;
                }
        }
        return result;
    }

    LRESULT Window::OnContextMenu (HWND hChild, LONG x, LONG y) {
        switch (GetDlgCtrlID (hChild)) {
            case ID::TABS_LISTS:
                // New
                // Rename
                // Merge to... POPUP
                // Delete
                break;

            case ID::TABS_VIEWS:
                if ((x == -1) && (y == -1)) {
                    // use 'current'
                    auto r = this->tabs.views->outline (this->tabs.views->current);
                    MapWindowPoints (hChild, HWND_DESKTOP, (POINT *) &r, 2);
                    x = r.left + metrics [SM_CXSMICON] / 2;
                    y = r.bottom - metrics [SM_CYSMICON] / 2;
                } else {
                    // use 'contextual'
                }

                // TODO: only disable duplicate/close menuitems for tabID < 0
                TrackPopupMenu (hMainTabsMenu, TPM_RIGHTBUTTON, x, y, 0, hWnd, NULL);
                break;
        }
        return 0;
    }

    LRESULT Window::OnMouse (UINT message, WPARAM modifiers, LONG x, LONG y) {
        RECT client;
        GetClientRect (hWnd, &client);

        auto element = 0;
        auto distance = 0;
        const auto w = metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
        const auto h = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];

        if (GetCapture () == hWnd) {
            switch (message) {
                case WM_LBUTTONUP:
                    this->drag.what = false;
                    ReleaseCapture ();
                    break;

                case WM_MOUSEMOVE:
                    element = this->drag.what;
                    switch (this->drag.what) { // confine to minimum and maximum position
                        case 0x201:
                            if (x > (client.right - dividers.right) - w) x = (client.right - dividers.right) - w;
                            if (x < w) x = w;
                            break;
                        case 0x202:
                            if (x > client.right) x = client.right;
                            if (x < dividers.left + w) x = dividers.left + w;
                            break;
                        case 0x103:
                            // TODO: properly limit
                            if (y > client.bottom - this->tabs.feeds->minimum.cy) y = client.bottom - this->tabs.feeds->minimum.cy;
                            if (y < extension + 4 * this->fonts.text.height) y = extension + 4 * this->fonts.text.height;
                            break;
                    }
                    switch ((this->drag.what & 0x0F00) >> 8) { // compute distance from previous position
                        case 2:
                            distance = (x - this->drag.x);
                            this->drag.x = x;
                            break;
                        case 1:
                            distance = (y - this->drag.y);
                            this->drag.y = y;
                            break;
                    }
                    if (distance) {
                        switch (this->drag.what) { // move
                            case 0x201:
                                dividers.left = dividers.left + distance;
                                if (dividers.left < metrics [SM_CXICON]) {
                                    dividers.left = metrics [SM_CXICON];
                                }
                                break;
                            case 0x202:
                                dividers.right = dividers.right - distance;
                                if (dividers.right < 0) {
                                    dividers.right = 0;
                                }
                                break;
                            case 0x103:
                                dividers.feeds = dividers.feeds + distance;
                                break;
                        }
                        // redraw
                        this->OnPositionChange (WINDOWPOS ());
                        if (ptrDwmExtendFrameIntoClientArea) {
                            ptrDwmExtendFrameIntoClientArea (hWnd, this->GetDwmMargins ());
                        }
                        RedrawWindow (hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
                    }
                    break;
            }
        } else {
            auto top = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + this->tabs.views->minimum.cy;
            auto bottom = client.bottom - minimum.statusbar.cy;

            if ((y > top) && (y < bottom)) {
                if (x >= dividers.left - w && x <= dividers.left) {
                    element = 0x201;
                }
                if (dividers.right > 0) {
                    if (x >= (client.right - dividers.right) && x <= (client.right - dividers.right) + w) {
                        element = 0x202;
                    }
                } else {
                    dividers.right = 0;
                }
            }

            if ((dividers.right > 0) && (x > (client.right - dividers.right) + w)) {
                if (y >= dividers.feeds && y <= (dividers.feeds + h)) {
                    element = 0x103;
                }
            }

            if (element) {
                switch (message) {
                    case WM_LBUTTONDOWN:
                        this->drag.what = element;
                        this->drag.x = x;
                        this->drag.y = y;
                        SetCapture (hWnd);
                        break;
                    case WM_LBUTTONDBLCLK:
                        switch (element) {
                            case 0x202:
                                this->OnCommand (BN_CLICKED, 0xF4, NULL);
                                break;
                        }
                        break;
                }
            }
        }

        switch (message) {
            case WM_MOUSEMOVE:
                switch ((element & 0x0F00) >> 8) {
                    case 2: SetCursor (cursor.horizontal); break;
                    case 1: SetCursor (cursor.vertical); break;
                    case 0: SetCursor (cursor.arrow); break;
                }
                break;
        }
        return 0;
    }

    bool IntersectRect (const RECT * r1, const RECT * r2) {
        RECT rTemp;
        return IntersectRect (&rTemp, r1, r2);
    }

    BOOL GetChildRect (HWND hParent, HWND hCtrl, LPRECT rcCtrl) {
        if (GetClientRect (hCtrl, rcCtrl)) {
            MapWindowPoints (hCtrl, hParent, reinterpret_cast <POINT *> (rcCtrl), 2u);
            return TRUE;
        } else
            return FALSE;
    }
    BOOL GetChildRect (HWND hParent, UINT id, LPRECT rcCtrl) {
        return GetChildRect (hParent, GetDlgItem (hParent, id), rcCtrl);
    }
    
    // TODO: computing proper metrics for various Windows versions and themes below is a mess;
    //       it might be nice to split into virtual classes simply switched on wm_themechange
    //       (at least windows version really doesn't need to be checked dozen times per redraw)

    void Window::BackgroundFill (HDC hDC, const RECT * rcArea, const RECT * rcClip, bool caption) {
        if (design.composited) {

            auto margins = this->GetDwmMargins ();
            RECT face = {
                rcArea->left + margins->cxLeftWidth,
                rcArea->top + margins->cyTopHeight,
                rcArea->right - margins->cxRightWidth,
                rcArea->bottom - margins->cyBottomHeight,
            };
            auto top = rcArea->top;
            auto transparent = (HBRUSH) GetStockObject (BLACK_BRUSH);

            if (margins->cyTopHeight) { RECT r = { rcClip->left, top, rcClip->right, face.top }; FillRect (hDC, &r, transparent); }
            if (margins->cxLeftWidth) { RECT r = { rcClip->left, top, face.left, rcClip->bottom }; FillRect (hDC, &r, transparent); }
            if (margins->cxRightWidth) { RECT r = { face.right, top, rcClip->right, rcClip->bottom }; FillRect (hDC, &r, transparent); }
            if (margins->cyBottomHeight) { RECT r = { rcClip->left, face.bottom, rcClip->right, rcClip->bottom }; FillRect (hDC, &r, transparent); }

            if (this->active) {
                SetDCBrushColor (hDC, design.colorization.active & 0x00FFFFFF);
            } else {
                SetDCBrushColor (hDC, design.colorization.inactive & 0x00FFFFFF);
            }

            IntersectRect (&face, &face, rcClip);
            FillRect (hDC, &face, (HBRUSH) GetStockObject (DC_BRUSH));
        } else {
            if (HANDLE hTheme = OpenThemeData (hWnd, VSCLASS_WINDOW)) {
                UINT type = this->active ? FS_ACTIVE : FS_INACTIVE;

                RECT rcLeft = {
                    rcArea->left - metrics [SM_CXFRAME],
                    rcArea->top + metrics [SM_CYFRAME] + metrics [SM_CYCAPTION],
                    rcArea->left + dividers.left,
                    rcArea->bottom
                };
                RECT rcRight = {
                    rcArea->left + dividers.left,
                    rcArea->top + metrics [SM_CYFRAME] + metrics [SM_CYCAPTION],
                    rcArea->right + metrics [SM_CXFRAME],
                    rcArea->bottom
                };
                if (IsWindowsVistaOrGreater ()) {
                    DrawThemeBackground (hTheme, hDC, WP_FRAMELEFT, type, &rcLeft, rcClip);
                    DrawThemeBackground (hTheme, hDC, WP_FRAMERIGHT, type, &rcRight, rcClip);
                } else {
                    FillRect (hDC, &rcLeft, GetSysColorBrush (COLOR_3DFACE));
                    FillRect (hDC, &rcRight, GetSysColorBrush (COLOR_3DFACE));
                }

                if (caption) {
                    RECT rcCaption = *rcArea;
                    rcCaption.bottom = rcCaption.top + metrics [SM_CYFRAME] + metrics [SM_CYCAPTION];
                    rcCaption.left -= metrics [SM_CXFRAME];
                    rcCaption.right += metrics [SM_CXFRAME];

                    if (IsZoomed (hWnd))
                        rcCaption.top += metrics [SM_CYFRAME];

                    if (IntersectRect (&rcCaption, rcClip))
                        DrawThemeBackground (hTheme, hDC, IsZoomed (hWnd) ? WP_MAXCAPTION : WP_CAPTION, type, &rcCaption, rcClip);
                }

                CloseThemeData (hTheme);
            } else {
                RECT rcFill = *rcArea;
                if (!caption) {
                    rcFill.top = metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
                }

                RECT rcIntersection;
                if (IntersectRect (&rcIntersection, &rcFill, rcClip)) {
                    FillRect (hDC, &rcIntersection, GetSysColorBrush (COLOR_BTNFACE));
                }

                RECT rcCaption = *rcArea;
                rcCaption.top += metrics [SM_CYFRAME];
                rcCaption.bottom = rcCaption.top + metrics [SM_CYCAPTION];

                if (IntersectRect (&rcCaption, rcClip)) {
                    BOOL gradient = FALSE;
                    SystemParametersInfo (SPI_GETGRADIENTCAPTIONS, 0, &gradient, 0);

                    DrawCaption (hWnd, hDC, &rcCaption, DC_TEXT | DC_ICON | (gradient ? DC_GRADIENT : 0) | (this->active ? DC_ACTIVE : 0));

                    auto w = metrics [SM_CXSIZE] - 2;
                    auto h = metrics [SM_CYSIZE] - 4;

                    RECT r;
                    r.top = rcCaption.top + (rcCaption.bottom - rcCaption.top) / 2 - h / 2;
                    r.bottom = r.top + h;
                    r.right = rcCaption.right - 2;
                    r.left = r.right - w;

                    DrawFrameControl (hDC, &r, DFC_CAPTION, DFCS_CAPTIONCLOSE);
                    OffsetRect (&r, -(w + 2), 0);
                    DrawFrameControl (hDC, &r, DFC_CAPTION, IsZoomed (hWnd) ? DFCS_CAPTIONRESTORE : DFCS_CAPTIONMAX);
                    OffsetRect (&r, -w, 0);
                    DrawFrameControl (hDC, &r, DFC_CAPTION, DFCS_CAPTIONMIN);
                }
            }
        }

        auto rListTabs = this->GetListsTabRect ();
        auto rList = GetListsFrame (rcArea, rListTabs);
        auto rRight = GetRightPane (*rcArea, rListTabs);
        auto rFeeds = GetFeedsFrame (rcArea, this->GetFeedsTabRect (rRight));
        auto rFilters = GetFiltersRect (rcArea, rRight);

        if (HANDLE hTheme = OpenThemeData (hWnd, VSCLASS_TAB)) {
            if (IsWindows10OrGreater () || !design.composited) {
                auto rPane = GetViewsFrame (rcArea);
                auto rPaneClip = GetTabControlClipRect (rPane);
                DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rPane, &rPaneClip);
            }

            auto rListClip = GetTabControlClipRect (rList);
            DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rList, &rListClip);
            auto rFeedsClip = GetTabControlClipRect (rFeeds);
            DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rFeeds, &rFeedsClip);
            auto rFiltersClip = GetTabControlClipRect (rFilters);
            DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rFilters, &rFiltersClip);
            CloseThemeData (hTheme);
        } else {
            auto rPane = GetViewsFrame (rcArea);

            DrawEdge (hDC, &rPane, BDR_SUNKEN, BF_RECT);
            DrawEdge (hDC, &rList, BDR_SUNKEN, BF_RECT);
            DrawEdge (hDC, &rFeeds, BDR_SUNKEN, BF_RECT);
            DrawEdge (hDC, &rFilters, BDR_SUNKEN, BF_RECT);
        }
    }

    RECT Window::GetViewsFrame (const RECT * rcArea) {
        RECT r = *rcArea;
        r.top += this->GetDwmMargins ()->cyTopHeight - 1;
        r.left += dividers.left - 1;
        r.right -= dividers.right - 1;
        r.bottom -= metrics [SM_CYCAPTION] + 1;

        if (IsAppThemed ()) {
            if (!IsZoomed (hWnd)) {
                if (!design.contrast) {
                    if (!dividers.right) {
                        r.right -= metrics [SM_CXPADDEDBORDER];
                    }
                }
            }
            if (design.nice) {
                r.bottom -= metrics [SM_CYFRAME];
            }
        } else {
            r.top -= 1;
        }
        return r;
    }

    RECT Window::GetTabControlContentRect (RECT r) {
        if (IsAppThemed ()) {
            r = GetTabControlClipRect (r);
            r.bottom += metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
            InflateRect (&r, -1, -1);
            if (!IsWindowsVistaOrGreater ()) {
                r.right -= 2;
                r.bottom -= this->extension + metrics [SM_CYFRAME] - 2; // why???
            }
        } else {
            InflateRect (&r, -2, -2);
        }
        return r;
    }

    RECT Window::GetListsFrame (const RECT * rcArea, const RECT & rListTabs) {
        RECT r = *rcArea;
        r.top += this->GetDwmMargins ()->cyTopHeight + this->tabs.views->minimum.cy - 1;
        r.bottom -= metrics [SM_CYCAPTION] + 1;
        r.left += rListTabs.left;
        r.right = r.left + rListTabs.right;

        if (IsAppThemed ()) {
            if (design.nice) {
                r.bottom -= metrics [SM_CYFRAME];
            }
            if (design.composited && !IsWindows10OrGreater ()) {
                r.bottom += metrics [SM_CYFRAME] + metrics [SM_CYEDGE] + 1;
                if (!IsWindows8OrGreater ()) {
                    r.bottom += 1;
                }
            }
        } else {
            r.top -= 1;
            r.left -= 2;
        }
        return r;
    }

    RECT Window::GetRightPane (const RECT & client, const RECT & rListTabs) {
        RECT r = rListTabs;
        r.left = (client.right - client.left) - dividers.right + metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
        r.right = (client.right - client.left) - r.left;
        if (!IsZoomed (hWnd)) {
            r.right -= metrics [SM_CXPADDEDBORDER];
        }
        return r;
    }

    RECT Window::GetFiltersRect (const RECT * rcArea, const RECT & rRightPane) {
        RECT r = *rcArea;
        r.bottom = r.top + LONG (dividers.feeds);
        r.top += rRightPane.top + this->tabs.views->minimum.cy - 1 + metrics [SM_CYFRAME];
        r.left += rRightPane.left;
        r.right = r.left + rRightPane.right;
        if (design.nice && !design.contrast) {
            r.right += 2;
        }
        return r;
    }

    RECT Window::GetFeedsFrame (const RECT * rcArea, const RECT & rFeedsTabs) {
        RECT r = *rcArea;
        r.top += rFeedsTabs.top + this->tabs.views->minimum.cy - 1;
        r.bottom -= metrics [SM_CYCAPTION] + 1;
        r.left += rFeedsTabs.left;
        r.right = r.left + rFeedsTabs.right;

        if (IsAppThemed ()) {
            if (design.nice) {
                r.bottom -= metrics [SM_CYFRAME];
            }
            if (design.composited && !IsWindows10OrGreater ()) {
                r.bottom += metrics [SM_CYFRAME] + metrics [SM_CYEDGE] + 1;
                if (!IsWindows8OrGreater ()) {
                    r.bottom += 1;
                }
            }
        } else {
            r.top -= 1;
            r.left -= 2;
        }
        if (design.nice && !design.contrast) {
            r.right += 2;
        }
        return r;
    }

    RECT Window::GetTabControlClipRect (RECT r) {
        if (design.nice && !design.contrast) {
            r.right -= 2;
            r.bottom -= 1;
        }
        return r;
    }

    LRESULT Window::OnControlPrePaint (HDC hDC, HWND hControl) {
        RECT rWindow;
        RECT rControl;

        GetWindowRect (hWnd, &rWindow);
        GetWindowRect (hControl, &rControl);

        OffsetRect (&rWindow, -rControl.left, -rControl.top);
        OffsetRect (&rControl, -rControl.left, -rControl.top);
        rWindow.left += metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
        rWindow.right -= metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
        rWindow.bottom -= metrics [SM_CYFRAME];

        this->BackgroundFill (hDC, &rWindow, &rControl, true);
        return (LRESULT) GetStockObject (NULL_BRUSH);
    }

    LRESULT Window::OnPaint () {
        PAINTSTRUCT ps;
        if (HDC hScreenDC = BeginPaint (hWnd, &ps)) {
            HDC hDC = NULL;
            HPAINTBUFFER hBuffer = NULL;

            RECT client;
            GetClientRect (hWnd, &client);

            if (ptrBeginBufferedPaint) {
                static const BP_PAINTPARAMS erase = { sizeof (BP_PAINTPARAMS), BPPF_ERASE, NULL, NULL };
                RECT r = client;
                if (design.nice && !design.composited) {
                    r.top = metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
                }
                if (!IsAppThemed ()) {
                    r.top = metrics [SM_CYFRAME];
                }
                hBuffer = ptrBeginBufferedPaint (hScreenDC, &r, BPBF_TOPDOWNDIB, &erase, &hDC);
            }
            if (!hBuffer) {
                hDC = hScreenDC;
            }

            this->BackgroundFill (hDC, &client, &ps.rcPaint, false);

            if (design.composited) {
                POINT iconPos = { 0, metrics [SM_CYFRAME] };
                SIZE iconSize = GetIconMetrics (StartIconSize);

                if (IsWindows10OrGreater ()) {
                    if (IsZoomed (hWnd) || design.contrast) {
                        iconPos.y += metrics [SM_CXPADDEDBORDER];
                    } else {
                        iconPos.x += metrics [SM_CXPADDEDBORDER];
                    }
                }

                if (HANDLE hTheme = OpenThemeData (hWnd, VSCLASS_WINDOW)) {
                    COLORREF color;
                    UINT glow;

                    if (this->GetCaptionTextColor (color, glow)) { // text is bright
                        for (auto & tc : this->alltabs) {
                            tc->buttons.color = 0xD0D0D0;
                            tc->buttons.hot = 0xFFFFFF;
                        }
                    } else {
                        for (auto & tc : this->alltabs) {
                            tc->buttons.color = color;
                            tc->buttons.hot = GetSysColor (COLOR_HOTLIGHT);
                        }
                    }
                    for (auto & tc : this->alltabs) {
                        tc->buttons.down = 0x7F7F7F;
                        tc->buttons.glow = glow;
                    }

                    const wchar_t * title = nullptr;
                    const auto title_length = LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase), 1, (wchar_t *) &title, 0);
                    const wchar_t * subtitle = nullptr;
                    const auto subtitle_length = LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase), 2, (wchar_t *) &subtitle, 0);

                    RECT r;
                    r.top = iconPos.y;
                    r.left = iconPos.x + metrics [SM_CXFRAME] + iconSize.cx;
                    r.right = dividers.left - metrics [SM_CXFRAME];
                    r.bottom = r.top + this->extension - metrics [SM_CYFRAME] / 2;

                    DrawCompositedTextDIB (hDC, hTheme, this->fonts.tiny.handle, subtitle, subtitle_length,
                                           DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, r, color, glow);
                    DrawCompositedTextDIB (hDC, hTheme, this->fonts.tabs.handle, title, title_length,
                                           DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, r, color, glow);

                    CloseThemeData (hTheme);
                }
                DrawIconEx (hDC, iconPos.x, iconPos.y, this->icons [StartIconSize], 0, 0, 0, NULL, DI_NORMAL);
            }

            // TODO: draw button floating over right edge of content (show/hide right panel)

            // TODO: draw horizontal splitter grips
            // TODO: draw bottom right size grip
            // TODO: draw status bar text/icon parts if "design.nice"

            if (hBuffer) {
                ptrEndBufferedPaint (hBuffer, TRUE);
            }

            EndPaint (hWnd, &ps);
        }
        return 0;
    }
}

#ifdef CRT_STATIC
extern "C" char * __cdecl __unDName (void *, const void *, int, void *, void *, unsigned short) {
    return nullptr;
}
extern "C" LCID __cdecl __acrt_DownlevelLocaleNameToLCID (LPCWSTR localeName) {
    return 0;
}
extern "C" int __cdecl __acrt_DownlevelLCIDToLocaleName (
    LCID   lcid,
    LPWSTR outLocaleName,
    int    cchLocaleName
) {
    return 0;
}
#endif
