#ifndef RADDI_OPTIONS_H
#define RADDI_OPTIONS_H

#include <cwchar>
#include <cwctype>
#include "uuid.h"

template <typename F, std::size_t N>
std::size_t option_nth (std::size_t i, unsigned long argc, wchar_t ** argw, const wchar_t (&name) [N], F f) {
    const auto length = (sizeof name / sizeof name [0]) - 1;

    auto skip = 0;
    switch (argw [i][0]) {
        case L'/':
            skip = 1;
            break;
        case L'-':
            while (argw [i][skip] == '-') {
                ++skip;
            }
            break;
    }

    if (std::wcsncmp (&argw [i][skip], name, length) == 0) {
        switch (argw [i][length + skip]) {
            case L':':
                raddi::log::note (raddi::component::main, 1, name, &argw [i][length + skip + 1]);
                f (&argw [i][length + skip + 1]);
                return true;

            case L'\0':
                raddi::log::note (raddi::component::main, 1, name, &argw [i][length + skip]);
                f (&argw [i][length + skip]);
                return true;
        }
    }
    return false;
}

template <typename F, std::size_t N>
std::size_t options (unsigned long argc, wchar_t ** argw, const wchar_t (&name) [N], F f) {
    std::size_t n = 0;
    for (auto i = 1uL; i != argc; ++i) {
        n += option_nth (i, argc, argw, name, f);
    }
    return n;
}

template <std::size_t N>
const wchar_t * option (unsigned long argc, wchar_t ** argw, const wchar_t (&name) [N], const wchar_t * default_ = nullptr) {
    options (argc, argw, name,
             [&default_](const wchar_t * result) { default_ = result;  });
    return default_;
}

static inline void option_convert (const wchar_t * p, bool & v) { 
    v = true;

    if (*p) {
        switch (std::towlower (*p)) {
            case L'f':
            case L'n':
            case L'o':
            case L'x':
                v = false;
                return;
        }
        try {
            if (std::stold (p) == 0.0L) {
                v = false;
                return;
            }
        } catch (...) {}

        try {
            if (std::stoll (p, nullptr, 0) == 0LL) {
                v = false;
                return;
            }
        } catch (...) {}
    }
}
static inline void option_convert (const wchar_t * p, int & v) { v = std::stoi (p); }
static inline void option_convert (const wchar_t * p, unsigned int & v) { v = std::stoul (p); }
static inline void option_convert (const wchar_t * p, long & v) { v = std::stol (p); }
static inline void option_convert (const wchar_t * p, unsigned long & v) { v = std::stoul (p); }
static inline void option_convert (const wchar_t * p, long long & v) { v = std::stoll (p); }
static inline void option_convert (const wchar_t * p, unsigned long long & v) { v = std::stoull (p); }
static inline void option_convert (const wchar_t * p, float & v) { v = std::stof (p); }
static inline void option_convert (const wchar_t * p, double & v) { v = std::stod (p); }
static inline void option_convert (const wchar_t * p, long double & v) { v = std::stold (p); }
static inline void option_convert (const wchar_t * p, std::wstring & v) { v = p; }
static inline void option_convert (const wchar_t * p, uuid & v) { v.parse (p); }

template <typename T, std::size_t N>
void option (unsigned long argc, wchar_t ** argw, const wchar_t (&name) [N], T & value) {
    if (auto p = option (argc, argw, name)) {
        try {
            option_convert (p, value);
        } catch (const std::invalid_argument &) {
        } catch (const std::out_of_range &) {}
    }
}

#endif
