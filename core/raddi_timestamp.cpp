#include <windows.h>
#include <cstring>
#include <cstdio>

#include "raddi_timestamp.h"

namespace {
    
    // difference of 1.1.2018 00:00:00 - January 1, 1601 UTC in seconds (unix timestamp minus 0x5a497a00)
    static const auto base = 0x3105a0b00uLL;

    // difference of 1.1.1970 00:00:00 - January 1, 1601 UTC in seconds
    // static const auto base = 0x2b6109100uLL;

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
    return static_cast <std::uint32_t> (u.ns / 1'000'000'0uLL - base);
}

std::uint32_t raddi::timestamp (const std::tm & tm) {
    return raddi::timestamp (tm2st (tm));
}

std::uint64_t raddi::microtimestamp () {
    uULL u;
    if (pfnGetSystemTime == nullptr) {
        if (auto p = GetProcAddress (GetModuleHandle (L"KERNEL32"), "GetSystemTimePreciseAsFileTime")) {
            pfnGetSystemTime = reinterpret_cast <void (WINAPI *) (LPFILETIME)> (p);
        } else {
            pfnGetSystemTime = GetSystemTimeAsFileTime;
        }
    }
    pfnGetSystemTime (&u.ft);
    return static_cast <std::uint64_t> (u.ns / 10uLL - base * 1'000'000uLL);
}

std::uint32_t raddi::timestamp (const SYSTEMTIME & st) {
    uULL u;
    if (SystemTimeToFileTime (&st, &u.ft))
        return static_cast <std::uint32_t> (u.ns / 1'000'000'0uLL - base);
    else
        return 0u;
}

std::tm raddi::time (std::uint32_t t) {
    return st2tm (raddi::wintime (t));
}

SYSTEMTIME raddi::wintime (std::uint32_t t) {
    SYSTEMTIME st;
    uULL u;
    u.ns = (t + base) * 1'000'000'0uLL;
    FileTimeToSystemTime (&u.ft, &st);
    return st;
}
SYSTEMTIME raddi::wintime (std::uint64_t t) {
    SYSTEMTIME st;
    uULL u;
    u.ns = t * 10uLL + base * 1'000'000'0uLL;
    FileTimeToSystemTime (&u.ft, &st);
    return st;
}

bool raddi::older (std::uint32_t timestamp, std::uint32_t reference) {
    return (timestamp - reference) > 0x80000000u;
}
