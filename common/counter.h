#ifndef RADDI_COUNTER_H
#define RADDI_COUNTER_H

#include <cstddef>
#include "log.h"

// counter
//  - platform-specific non-blocking atomic operation counter, see 'server.cpp' for typical usage
//
struct counter {
    unsigned long long n = 0;
    unsigned long long bytes = 0;

    void operator += (std::size_t value) noexcept;
};

// translate
//  - for passing Counters as a log function parameter
//
inline std::wstring translate (const counter & c, const std::wstring &) {
    static const char prefix [] = { 'B', 'k', 'M', 'G', 'T', 'P', 'E' };

    auto m = 0;
    auto v = (double) c.bytes;
    while (v >= 922) {
        v /= 1024.0;
        ++m;
    }
    wchar_t number [64];
    std::swprintf (number, sizeof number / sizeof number [0], L"%llu (%.*f %c%s)",
                   c.n, (v < 10.0 && c.bytes > 10), v, prefix [m], (m != 0) ? L"B" : L"");
    return number;
}

#endif
