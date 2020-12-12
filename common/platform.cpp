#include "platform.h"
#include <algorithm>
#include <cwctype>

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

bool IsPathAbsolute (std::wstring_view path) {
    return ((path.length () >= 4) && std::iswalpha (path [0]) && path [1] == L':' && path [2] == L'\\')
        || ((path.length () >= 5) && path [0] == L'\\' && path [1] == L'\\')
        ;
}

bool GetProcessSessionId (DWORD pid, DWORD * id) {
    if (auto hKernel32 = GetModuleHandle (L"KERNEL32")) {
        BOOL (WINAPI * ptrProcessIdToSessionId) (DWORD, DWORD *);
        if (Symbol (hKernel32, ptrProcessIdToSessionId, "ProcessIdToSessionId")) { // NT 5.2+
            if (ptrProcessIdToSessionId (pid, id))
                return true;
        }
    }
    return false;
}

DWORD GetCurrentProcessSessionId () {
    DWORD id;
    if (GetProcessSessionId (GetCurrentProcessId (), &id))
        return id;

#if defined (_M_AMD64)
    return (DWORD) *reinterpret_cast <std::uint64_t *> (__readgsqword (0x60) + 0x02C0);
#elif defined (_M_IX86)
    __asm mov eax, fs:[0x18];
    __asm mov eax, [eax + 0x30];
    __asm mov eax, [eax + 0x1d4];
#else
    return 0;
#endif
}
