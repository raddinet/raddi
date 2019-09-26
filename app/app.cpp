#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
#include <winternl.h>

#include <stdexcept>
#include <cstdarg>
#include <list>
#include <map>

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

#include "../common/errorbox.h"
#include "searchbox.h"
#include "window.h"
#include "menus.h"
#include "tabs.h"
#include "data.h"
#include "../common/node.h"
#include "resolver.h"

#pragma warning (disable:6053) // snprintf may not NUL terminate
#pragma warning (disable:28159) // GetTickCount64 suggestion

extern "C" IMAGE_DOS_HEADER __ImageBase;
const VS_FIXEDFILEINFO * const version = GetCurrentProcessVersionInfo ();

DWORD gui = 0; // global ID of GUI thread

uuid app;
Data database;
Node connection;
Resolver resolver;

Design design;
Cursors cursor;

alignas (std::uint64_t) char raddi::protocol::magic [8] = "RADDI/1";

namespace {
    LPCTSTR InitializeGUI (HINSTANCE hInstance);
    bool InteractiveAppDataInitialization ();
    void GuiThreadMessageProcedure (const MSG &);
    bool CreateWindows (HINSTANCE hInstance, LPCTSTR atom, int mode);

    DWORD idGuiChangesCoalescingTimer = 0;
}

int CALLBACK wWinMain (_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
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

    ::gui = GetCurrentThreadId ();
    
    // TODO: if portable installation, check database for option to run own local instance

    if (!connection.initialize (option (__argc, __wargv, L"instance"), WM_APP_NODE_STATE, WM_APP_NODE_UPDATE)) {
        return StopBox (0x0C);
    }
    if (!resolver.initialize (WM_APP_TITLE_RESOLVED)) {
        // TODO: log error, but continue...
    }
    
    if (auto atom = InitializeGUI (hInstance)) {
        cursor.update ();
        design.update ();

        idGuiChangesCoalescingTimer = SetTimer (NULL, 0, USER_TIMER_MAXIMUM, NULL);

        // register for restart in case of crash or windows update
        //  - TODO: RegisterApplicationRecoveryCallback/SetUnhandledExceptionFilter
        //          to save unsent pending entries, and save unfinished replies

        Optional <HRESULT, PCWSTR, DWORD> (L"KERNEL32", "RegisterApplicationRestart", L"recover", 0);

        if (database.identities.size.query <int> () == 0) {
            // TODO: if not connected, wait
            // TODO: dialog
            // ...quit if cancelled?
        }

        if (CreateWindows (hInstance, atom, nCmdShow)) {
            if (ptrChangeWindowMessageFilter) {
                ptrChangeWindowMessageFilter (WM_APP_INSTANCE_NOTIFY, MSGFLT_ADD);
            }

            // all windows created, start getting db change notifications
            connection.start ();
            resolver.start ();

            // start processing messages
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
                    GuiThreadMessageProcedure (message);
                }
            }

            database.close ();
            resolver.terminate ();
            connection.terminate ();
            
            CoUninitialize ();
            return (int) message.wParam;
        }
    }

    return GetLastError ()
         ? StopBox (0)
         : ERROR_SUCCESS;
}

namespace {
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

        InitializeMenus (hInstance);
        InitializeSearchBox (hInstance);

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
        if (ptrAllowDarkModeForApp) {
            ptrAllowDarkModeForApp (true);
        }

        SetThemeAppProperties (STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS);
        return Window::Initialize (hInstance);
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

            // write current version to assist when dynamically upgrading in the future
            database.execute (L"REPLACE INTO `app` (`property`,`value`) VALUES ('version',?)", version->dwFileVersionMS);
            
            return true;
        } catch (const SQLite::Exception & x) {
            StopBox (5, x.what ());
            return false;
        }
    }

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
                                          D, D, D, D, HWND_DESKTOP, NULL, hInstance, (LPVOID) 1)) {
                ShowWindow (hWnd, nCmdShow);
                any = true;
            } else {
                StopBox (0x09);
            }
        }
        return any;
    }

    void BroadcastMessage (UINT message) {
        EnumThreadWindows (GetCurrentThreadId (),
                           [](HWND hWnd, LPARAM message)->BOOL {
                               return PostMessage (hWnd, (UINT) message, 0, 0);
                           }, message);
    }
    void BroadcastMessage (const MSG & message) {
        EnumThreadWindows (GetCurrentThreadId (),
                           [](HWND hWnd, LPARAM message_)->BOOL {
                               auto m = reinterpret_cast <const MSG *> (message_);
                               return PostMessage (hWnd, m->message, m->wParam, m->lParam);
                           }, reinterpret_cast <LPARAM> (&message));
    }

    void OnFullAppCommand (WPARAM id);

    void GuiThreadMessageProcedure (const MSG & message) {
        switch (message.message) {

            case WM_COMMAND:
                return OnFullAppCommand (message.wParam);

            case WM_APP_GUI_THEME_CHANGE:
                SetTimer (NULL, idGuiChangesCoalescingTimer, USER_TIMER_MINIMUM, NULL);
                break;

            case WM_TIMER: // note that we don't call DispatchMessage so timers' procedures are not called
                if (message.wParam == idGuiChangesCoalescingTimer) {
                    cursor.update ();
                    design.update ();
                    BroadcastMessage (WM_APP_GUI_THEME_CHANGE);
                    SetTimer (NULL, idGuiChangesCoalescingTimer, USER_TIMER_MAXIMUM, NULL);
                }
                break;

            case WM_APP_INSTANCE_NOTIFY: // notifications from other instances 
                switch (message.wParam) {
                    case 0: // request to process 'requests' table
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

                                                       // TODO: is eid open in this window? then activate (and scroll) and return FALSE

                                                       return TRUE;
                                                   }, 0)) {
                                // all returned TRUE, direct last active window to open 'entry' in new tab
                            }
                        }
                        break;
                }
                break;

            case WM_APP_NODE_STATE: // node connection
                switch (message.wParam) {
                    case 0: // disconnected
                        break;
                    case 1: // connected
                        resolver.advance ();
                        break;
                    case 2: // exception, lParam is detail
                        return; // do not broadcast to windows
                }
                BroadcastMessage (message);
                break;

            case WM_APP_NODE_UPDATE + (UINT) Node::table::identities:
            case WM_APP_NODE_UPDATE + (UINT) Node::table::channels:

            case WM_APP_NODE_UPDATE + (UINT) Node::table::threads:
            case WM_APP_NODE_UPDATE + (UINT) Node::table::data:
                resolver.advance ((Node::table) (message.message - WM_APP_NODE_UPDATE), message.wParam, message.lParam);
                // TODO: for data enum all new entries, enum windows, enum views, if relevant, signal update
                break;

/*
                switch (message.wParam) {
                    case 0:
                    case 2:
                        if (message.lParam) {
                            std::size_t n [3] = { 0, 0, 0 };

                            if (connection.connected ()) {
                                exclusive guard (connection.lock);

                                if ((message.wParam == 0) || (message.lParam == 2) || (message.lParam == 1)) {
                                    n[0] = connection.database->threads->count ();
                                    n[1] = connection.database->channels->count ();
                                }
                                if ((message.wParam == 0) || (message.lParam == 3)) {
                                    n[2] = connection.database->identities->count ();
                                }
                            }

                            if ((n [0] && n [1]) || n [2]) {
                                EnumThreadWindows (GetCurrentThreadId (),
                                                   [](HWND hWnd, LPARAM n_)->BOOL {
                                                       auto * n = reinterpret_cast <std::size_t *> (n_);
                                                       if (n [0] && n [1]) {
                                                           SendMessage (hWnd, WM_APP_CHANNELS_COUNT, n [0], n [1]);
                                                       }
                                                       if (n [2]) {
                                                           SendMessage (hWnd, WM_APP_IDENTITIES_COUNT, n [2], 0);
                                                       }
                                                       return TRUE;
                                                   }, (LPARAM) & n);
                            }
                            // TODO: make List fetch data from data->top to raddi::now () + 1
                        }
                        break;
                }

                // BroadcastMessage (message);
                break;*/

            /*case WM_APP_TITLE_RESOLVED: // eid* wParam resolved as string (lParam ...std::wstring* ???)
                EnumThreadWindows (GetCurrentThreadId (),
                                   [] (HWND hWnd, LPARAM message_)->BOOL {
                                       SendMessage (hWnd,
                                                    reinterpret_cast <MSG *> (message_)->message,
                                                    reinterpret_cast <MSG *> (message_)->wParam,
                                                    reinterpret_cast <MSG *> (message_)->lParam);
                                       return TRUE;
                                   }, reinterpret_cast <LPARAM> (&message));
                // TODO: delete *lParam (std::wstring *) ??? or delete [] wchar_t?
                break;*/
        }
    }

    void OnFullAppCommand (WPARAM id) {
        switch (id) {
            case 0xCF: // close App
                EnumThreadWindows (GetCurrentThreadId (),
                                   [](HWND hWnd, LPARAM)->BOOL {
                                       DestroyWindow (hWnd);
                                       return TRUE;
                                   }, 0);
                break;
        }
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
