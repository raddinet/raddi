#ifndef RADDI_PLATFORM_H
#define RADDI_PLATFORM_H

#ifdef _WIN32
#ifdef _MSC_VER
#if defined(_M_ARM64)
#define ARCHITECTURE "AArch64"
#elif defined(_M_ARM)
#define ARCHITECTURE "AArch32"
#elif defined(_M_X64)
#define ARCHITECTURE "x86-64"
#else
#define ARCHITECTURE "x86-32"
#endif
#else
#if defined(_WIN64)
#define ARCHITECTURE "x86-64"
#else
#define ARCHITECTURE "x86-32"
#endif
#endif
#endif

static const char BUILD_TIMESTAMP [] = {
    __DATE__ [7], __DATE__ [8], __DATE__ [9], __DATE__ [10],
    '-',
    (__DATE__ [0] == 'O' || __DATE__ [0] == 'N' || __DATE__ [0] == 'D') ? '1' : '0',

    (__DATE__ [0] == 'J' && __DATE__ [1] == 'a' && __DATE__ [2] == 'n') ? '1' :
    (__DATE__ [0] == 'F' && __DATE__ [1] == 'e' && __DATE__ [2] == 'b') ? '2' :
    (__DATE__ [0] == 'M' && __DATE__ [1] == 'a' && __DATE__ [2] == 'r') ? '3' :
    (__DATE__ [0] == 'A' && __DATE__ [1] == 'p' && __DATE__ [2] == 'r') ? '4' :
    (__DATE__ [0] == 'M' && __DATE__ [1] == 'a' && __DATE__ [2] == 'y') ? '5' :
    (__DATE__ [0] == 'J' && __DATE__ [1] == 'u' && __DATE__ [2] == 'n') ? '6' :
    (__DATE__ [0] == 'J' && __DATE__ [1] == 'u' && __DATE__ [2] == 'l') ? '7' :
    (__DATE__ [0] == 'A' && __DATE__ [1] == 'u' && __DATE__ [2] == 'g') ? '8' :
    (__DATE__ [0] == 'S') ? '9' :
    (__DATE__ [0] == 'O') ? '0' :
    (__DATE__ [0] == 'N') ? '1' : '2',
    '-',
    (__DATE__ [4] != ' ') ? __DATE__ [4] : '0', __DATE__ [5],
    ' ',
    __TIME__ [0], __TIME__ [1], __TIME__ [2], __TIME__ [3], __TIME__ [4], __TIME__ [5], __TIME__ [6], __TIME__ [7]
};

#ifdef _WIN32
#include <windows.h>

// Windows API platform helper functions

DWORD GetLogicalProcessorCount ();
const VS_FIXEDFILEINFO * GetModuleVersionInfo (HMODULE);
const VS_FIXEDFILEINFO * GetCurrentProcessVersionInfo ();
bool IsWindowsBuildOrGreater (WORD wMajorVersion, WORD wMinorVersion, DWORD dwBuildNumber);

template <typename Return, typename... Parameters>
Return Optional (const wchar_t * module, const char * name, Parameters... parameters) {
    if (auto h = GetModuleHandle (module)) {
        if (auto p = reinterpret_cast <Return (WINAPI *) (Parameters...)> (GetProcAddress (h, name))) {
            raddi::log::note (2, module, name);
            return p (parameters...);
        } else {
            raddi::log::error (2, module, name);
        }
    } else {
        raddi::log::error (1, module);
    }
    return Return ();
}

#endif
#endif
