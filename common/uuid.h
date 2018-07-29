#ifndef RADDI_UUID_H
#define RADDI_UUID_H

#include <cstddef>
#include <string>
#include "log.h"

// uuid
//  - simple UUID usable as map key
//
class uuid {
    std::uint8_t data [16];

public:
    uuid ();
    uuid (const uuid &) = default;

    inline bool operator == (const uuid & other) const { return std::memcmp (this->data, other.data, 16) == 0; }
    inline bool operator != (const uuid & other) const { return std::memcmp (this->data, other.data, 16) != 0; }
    inline bool operator <  (const uuid & other) const { return std::memcmp (this->data, other.data, 16) <  0; }
    inline bool operator <= (const uuid & other) const { return std::memcmp (this->data, other.data, 16) <= 0; }
    inline bool operator >  (const uuid & other) const { return std::memcmp (this->data, other.data, 16) >  0; }
    inline bool operator >= (const uuid & other) const { return std::memcmp (this->data, other.data, 16) >= 0; }

    void null ();
    bool is_null () const;

    bool parse (const char *);
    bool parse (const wchar_t *);

    std::string c_cstr () const;
    std::wstring c_wstr () const;
};

// translate
//  - for passing uuid as a log function parameter
//
inline std::wstring translate (uuid v, const std::wstring &) {
    return v.c_wstr ();
}

#endif
