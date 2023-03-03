#include "winver.h"
#include <VersionHelpers.h>

extern "C" void WINAPI RtlGetNtVersionNumbers (LPDWORD, LPDWORD, LPDWORD); // NTDLL

std::uint8_t winver = 5;
std::uint16_t winbuild = 0;

void InitializeWinVer () {
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;

    RtlGetNtVersionNumbers (&major, &minor, &build);
    winbuild = (std::uint16_t) build;

    if (major >= 10 && winbuild >= 22000) winver = 11;
    else if (major >= 10)                 winver = 10;
    else if (major == 6 && minor >= 3)    winver = 9;
    else if (major == 6 && minor == 2)    winver = 8;
    else if (major == 6 && minor == 1)    winver = 7;
    else if (major == 6 && minor == 0)    winver = 6;
}
