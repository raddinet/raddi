#include <windows.h>
#include <wininet.h>
#include <commctrl.h>
#include <shlobj.h>

#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>

#include <VersionHelpers.h>

#include <stdexcept>
#include <cstdarg>
#include <cwctype>
#include <map>

#undef small

#include "../common/log.h"
#include "../common/platform.h"
#include "../common/threadpool.h"

int CALLBACK wWinMain (HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    
    // detect MSVC runtime version

    // detect if already installed (and what version)
    //  - if payload contains higher version, display "update" button and "install" (either "for me" or "system")
    //  - show "change/repair" button (future?)
    //     - mode to backup 
    //  - show "uninstall" button

    // Wizard:
    //  - NO hello page: message "just click next/next/next" on first page
    //  - service/tray/on demand (app starts node)
    //     - radio buttons with short explanation
    //     - default: service if elevated, tray otherwise
    //  - role
    //     - core / normal (default) / leaf
    //  - installation path:
    //     - if elevated suggest "program files", otherwise users appdata (yeah, sorry)
    //     - if directed to unwrittable path, restart itself elevated (remember choices)
    //     - if not present, and not elevated, ask user to install portable build?
    //  - shortcuts:
    //     - start menu (checked)
    //     - desktop (unchecked)
    //     - quicklaunch (show on XP ...and vista? ...checked if not installed as tray)
    //     - for all users / just for me
    //        - display only if elevated and installing service
    //  - done page (returned from elevated process (if any)):
    //     - checkbox to run the app

    // Process:
    //  - msvc runtime
    //     - if redist exe present, and elevated, run redist install (or update) / install / passive / norestart
    //     - if not present, redirect user to download if not installed
    //     - if not present, and not elevated, install portable build (redirect to download?)
    //     - warn if very old?

    //  - if overwriting running executable, shut it down
    //     - stop service or stop background task (tray?)

    //  - decompress to selected location
    //     - DO NOT OVERWRITE LOCAL raddi.db IF EXISTS (actually app itself should deploy/create the default one)
    //  - create service or autorun tray icon carrier process
    //  - add raddiXX.exe to firewall (cmd
    
    return 0;
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
