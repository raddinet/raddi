#include <windows.h>
#include <cstring>
#include <cstdio>

#include "raddi_timestamp.h"

namespace {

    // helper type punning union
    union uULL {
        unsigned long long ns;
        FILETIME           ft;
    };

    // converts Windows' SYSTEMTIME to C tm structure
    std::tm st2tm (const SYSTEMTIME & st) {
        std::tm tm;
        std::memset (&tm, 0, sizeof tm);
        tm.tm_year = st.wYear - 1900u;
        tm.tm_mon = st.wMonth - 1u;
        tm.tm_mday = st.wDay;
        tm.tm_wday = st.wDayOfWeek;
        tm.tm_hour = st.wHour;
        tm.tm_min = st.wMinute;
        tm.tm_sec = st.wSecond;
        return tm;
    }

    // converts C tm structure to Windows' SYSTEMTIME 
    SYSTEMTIME tm2st (const std::tm & tm) {
        SYSTEMTIME st;
        st.wYear = tm.tm_year + 1900u;
        st.wMonth = tm.tm_mon + 1u;
        st.wDay = tm.tm_mday;
        st.wDayOfWeek = tm.tm_wday;
        st.wHour = tm.tm_hour;
        st.wMinute = tm.tm_min;
        st.wSecond = tm.tm_sec;
        st.wMilliseconds = 0;
        return st;
    }

    void (WINAPI * pfnGetSystemTime) (LPFILETIME) = nullptr;
}

std::uint32_t raddi::timestamp () {
    uULL u;
    GetSystemTimeAsFileTime (&u.ft);
    return static_cast <std::uint32_t> (u.ns / 1'000'000'0uLL - timestamp_base);
}

std::uint32_t raddi::timestamp (const std::tm & tm) {
    return raddi::timestamp (tm2st (tm));
}

std::uint64_t raddi::microtimestamp () {
    uULL u;
    if (pfnGetSystemTime == nullptr) {
        if (auto hKernel32 = GetModuleHandle (L"KERNEL32")) {
            if (auto p = GetProcAddress (hKernel32, "GetSystemTimePreciseAsFileTime")) {
                pfnGetSystemTime = reinterpret_cast <void (WINAPI*) (LPFILETIME)> (p);
            }
        }
        if (pfnGetSystemTime == nullptr) {
            pfnGetSystemTime = GetSystemTimeAsFileTime;
        }
    }
    pfnGetSystemTime (&u.ft);
    return static_cast <std::uint64_t> (u.ns / 10uLL - timestamp_base * 1'000'000uLL);
}

std::uint32_t raddi::timestamp (const SYSTEMTIME & st) {
    uULL u;
    if (SystemTimeToFileTime (&st, &u.ft))
        return static_cast <std::uint32_t> (u.ns / 1'000'000'0uLL - timestamp_base);
    else
        return 0u;
}

std::tm raddi::time (std::uint32_t t) {
    return st2tm (raddi::wintime (t));
}

SYSTEMTIME raddi::wintime (std::uint32_t t) {
    SYSTEMTIME st;
    uULL u;
    u.ns = (t + timestamp_base) * 1'000'000'0uLL;
    FileTimeToSystemTime (&u.ft, &st);
    return st;
}
SYSTEMTIME raddi::wintime (std::uint64_t t) {
    SYSTEMTIME st;
    uULL u;
    u.ns = t * 10uLL + timestamp_base * 1'000'000'0uLL;
    FileTimeToSystemTime (&u.ft, &st);
    return st;
}
