#include "winver.h"
#include <VersionHelpers.h>

extern "C" void WINAPI RtlGetNtVersionNumbers (LPDWORD, LPDWORD, LPDWORD); // NTDLL

std::uint8_t winver = 5;
std::uint16_t winbuild = 0;

void InitializeWinVer () {
    if (IsWindowsBuildOrGreater (10, 0, 22000)) winver = 11;
    else if (IsWindows10OrGreater ())       winver = 10;
    else if (IsWindows8Point1OrGreater ())  winver = 9;
    else if (IsWindows8OrGreater ())        winver = 8;
    else if (IsWindows7OrGreater ())        winver = 7;
    else if (IsWindowsVistaOrGreater ())    winver = 6;

    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;

    RtlGetNtVersionNumbers (&major, &minor, &build);
    winbuild = (std::uint16_t) build;
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
