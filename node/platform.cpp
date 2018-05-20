#include "platform.h"
#include <algorithm>

DWORD GetLogicalProcessorCount () {
    SYSTEM_INFO si;
    GetSystemInfo (&si);

    if (si.dwNumberOfProcessors > MAXIMUM_PROC_PER_GROUP) {
        si.dwNumberOfProcessors = MAXIMUM_PROC_PER_GROUP;
    }
    if (si.dwNumberOfProcessors < 1) {
        si.dwNumberOfProcessors = 1;
    }
    return si.dwNumberOfProcessors;
}

const VS_FIXEDFILEINFO * GetModuleVersionInfo (HMODULE handle) {
    if (HRSRC hRsrc = FindResource (handle, MAKEINTRESOURCE (1), RT_VERSION))
        if (HGLOBAL hGlobal = LoadResource (handle, hRsrc)) {
            auto p = reinterpret_cast <const DWORD *> (LockResource (hGlobal));
            auto e = p + (SizeofResource (handle, hRsrc) - sizeof (VS_FIXEDFILEINFO)) / sizeof *p;

            p = std::find (p, e, 0xFEEF04BDu);
            if (p != e)
                return reinterpret_cast <const VS_FIXEDFILEINFO *> (p);
        }

    return nullptr;
}

const VS_FIXEDFILEINFO * GetCurrentProcessVersionInfo () {
    return GetModuleVersionInfo (NULL);
}

bool IsWindowsBuildOrGreater (WORD wMajorVersion, WORD wMinorVersion, DWORD dwBuildNumber) {
    OSVERSIONINFOEXW osvi = { sizeof (osvi), 0, 0, 0, 0, { 0 }, 0, 0 };
    DWORDLONG mask = 0;

    mask = VerSetConditionMask (mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    mask = VerSetConditionMask (mask, VER_MINORVERSION, VER_GREATER_EQUAL);
    mask = VerSetConditionMask (mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = wMajorVersion;
    osvi.dwMinorVersion = wMinorVersion;
    osvi.dwBuildNumber = dwBuildNumber;

    return VerifyVersionInfoW (&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, mask) != FALSE;
}
