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
    // TODO
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
