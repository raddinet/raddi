#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>

#include <VersionHelpers.h>

#include <stdexcept>
#include <cstdarg>
#include <list>

#include <sodium.h>
#include <lzma.h>
#include <sqlite3.h>

#include "../common/log.h"
#include "../common/lock.h"
#include "../common/file.h"
#include "../common/options.h"
#include "../common/platform.h"

#include "../core/raddi.h"
#include "../core/raddi_content.h"

uuid app;

alignas (std::uint64_t) char raddi::protocol::magic [8] = "RADDI/1";
const VS_FIXEDFILEINFO * const version = GetCurrentProcessVersionInfo ();

int CALLBACK wWinMain (HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    ULONG heapmode = 2;
    HeapSetInformation (GetProcessHeap (), HeapCompatibilityInformation, &heapmode, sizeof heapmode);

    sodium_init ();
    sqlite3_initialize ();
    
    raddi::log::display (L"disabled"); // TODO: redirect to some internal history
    raddi::log::initialize (option (__argc, __wargv, L"log"), L"\\RADDI.net\\", L"app", false);

    raddi::log::event (0x01,
                       (unsigned long) HIWORD (version->dwProductVersionMS),
                       (unsigned long) LOWORD (version->dwProductVersionMS),
                       ARCHITECTURE, BUILD_TIMESTAMP);

    raddi::log::note (0x05, "sqlite3", sqlite3_libversion (), GetModuleHandle (L"sqlite3") ? L"dynamic" : L"static");
    raddi::log::note (0x05, "liblzma", lzma_version_string (), GetModuleHandle (L"liblzma") ? L"dynamic" : L"static");
    raddi::log::note (0x05, "libsodium", sodium_version_string (), GetModuleHandle (L"libsodium") ? L"dynamic" : L"static");

#ifdef CRT_STATIC
    raddi::log::note (0x05, "vcruntime", CRT_STATIC, L"static");
#else
    if (auto h = GetModuleHandle (L"VCRUNTIME140")) {
        if (auto info = GetModuleVersionInfo (h)) {
            wchar_t vs [32];
            std::swprintf (vs, sizeof vs / sizeof vs [0], L"%u.%u.%u",
                           (unsigned long) HIWORD (info->dwFileVersionMS),
                           (unsigned long) LOWORD (info->dwFileVersionMS),
                           (unsigned long) HIWORD (info->dwFileVersionLS));
            raddi::log::note (0x05, "vcruntime", vs, L"dynamic");
        }
    }
#endif
    raddi::log::event (0xF0, raddi::log::path);
    
    // apps need their UUID for daemon to distinguish them:
    //  - either hard-code the UUID, every app different!
    //  - or generate on first run and store in app's settings, like this:

    HKEY registry;
    if (RegCreateKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\RADDI.net", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &registry, NULL) == ERROR_SUCCESS) {
        DWORD size = sizeof app;
        if (RegQueryValueEx (registry, L"app", NULL, NULL, (LPBYTE) &app, &size) != ERROR_SUCCESS) {
            RegSetValueEx (registry, L"app", 0, REG_BINARY, (LPBYTE) &app, sizeof app);
            raddi::log::event (0x02, app);
        }
        RegCloseKey (registry);
        registry = NULL;
    }




    return GetLastError ();
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
