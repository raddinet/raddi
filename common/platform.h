#ifndef RADDI_PLATFORM_H
#define RADDI_PLATFORM_H

#ifdef _WIN32
#ifdef _MSC_VER
#if defined(_M_ARM64)
#define ARCHITECTURE "arm-64"
#elif defined(_M_ARM)
#define ARCHITECTURE "arm-32"
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

#ifdef USING_WINSQLITE
#define SQLITE3_DLL_NAME L"WINSQLITE3"
#define SQLITE3_DLL_TYPE L"system"
#else
#define SQLITE3_DLL_NAME L"sqlite3.dll"
#define SQLITE3_DLL_TYPE L"dynamic"
#endif

#define STRINGIFY(S)    #S
#define MAKESTRING(M,L) M(L)
#define STRINGIZE(X)    MAKESTRING(STRINGIFY,X)

#ifdef _MSC_FULL_VER
#define COMPILER "MSVC++ " STRINGIZE(_MSC_FULL_VER)
#else
#define COMPILER ""
#endif

#ifdef _WIN32
#include <windows.h>
#include <string_view>
#include <vector>
#include "log.h"
#include "winver.h"

// Windows API platform helper functions

void InitPlatformAPI ();

DWORD GetLogicalProcessorCount ();
DWORD GetCurrentProcessSessionId ();

bool GetProcessSessionId (DWORD id, DWORD *);
const VS_FIXEDFILEINFO * GetModuleVersionInfo (HMODULE);
const VS_FIXEDFILEINFO * GetCurrentProcessVersionInfo ();
bool IsPathAbsolute (std::wstring_view);

struct Processor {
    union {
        struct {
            WORD group;
            BYTE number; // SMT 0..63 (or less) in group (first thread of a core)
            BYTE eclass : 2; // P/E higher number means fastest CORE
            BYTE smt    : 6; // SMT thread within CORE
        };
        DWORD spec = 0;
    };
    KAFFINITY affinity = 0; // module or L2 affinity

public:
    unsigned int rank () const {
        return ((this->smt * 2) + (this->eclass * this->eclass));
    }
    unsigned int order () const {
        return (this->rank () << 24)
             + (this->group << 8)
             + (this->number << 0);
    }

    bool operator == (const Processor & other) const { return this->spec == other.spec; }
    bool operator < (const Processor & other) const { return this->order () < other.order (); }
};

bool IsHomogeneousSystem ();
std::size_t GetPredominantSMT ();
std::vector <Processor> GetRankedLogicalProcessorList ();

BOOL AssignThreadLogicalProcessor (HANDLE hThread, Processor processor);
BOOL SetThreadIdealLogicalProcessor (HANDLE hThread, Processor processor);

template <typename P>
bool Symbol (HMODULE h, P & pointer, const char * name) {
    if (P p = reinterpret_cast <P> (GetProcAddress (h, name))) {
        pointer = p;
        return true;
    } else
        return false;
}

template <typename P>
bool Symbol (HMODULE h, P & pointer, USHORT index) {
    if (P p = reinterpret_cast <P> (GetProcAddress (h, MAKEINTRESOURCEA (index)))) {
        pointer = p;
        return true;
    } else
        return false;
}

template <typename Return, typename... Parameters>
Return Optional (const wchar_t * m, const char * name, Parameters... parameters) {
    if (auto h = GetModuleHandle (m)) {
        if (auto p = reinterpret_cast <Return (WINAPI *) (Parameters...)> (GetProcAddress (h, name))) {
            raddi::log::note (2, m, name);
            return p (parameters...);
        } else {
            raddi::log::error (2, m, name);
        }
    } else {
        raddi::log::error (1, m);
    }
    return Return ();
}

#endif
#endif
