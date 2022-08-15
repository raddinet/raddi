#ifndef RADDI_IID_H
#define RADDI_IID_H

#include "raddi_timestamp.h"

#include <cstddef>
#include <string>

namespace raddi {

    // iid
    //  - identity (user account) identifier
    // 
    struct iid {
        std::uint32_t timestamp;    // identity creation time
        std::uint32_t nonce;        // hash of identity's public keys

        // serialize
        //  - generates displayable representation of the iid object
        //  - buffer length must be at least 17 characters
        //
        std::size_t serialize (wchar_t * buffer, std::size_t size) const;

        // serialize
        //  - generates std::wstring containing displayable representation of the iid object
        //
        inline std::wstring serialize () const {
            wchar_t buffer [iid::max_length + 1];
            this->serialize (buffer, sizeof buffer / sizeof buffer [0]);
            return buffer;
        }

        // parse
        //  - attempts to parse serialized iid, ignoring whitespace
        //  - contents of the iid object are undefined on failure
        //  - returns number of characters parsed on success, zero on failure
        //
        std::size_t parse (const wchar_t *);
        std::size_t parse (const char *);

        // isnull
        //  - are both members 0, thus null, invalid, special meaning
        //
        inline bool isnull () const {
            return this->timestamp == 0
                && this->nonce == 0;
        }

        // erased
        //  - deletion within database is done by zeroing the record
        //
        inline bool erased () const {
            return this->isnull ();
        }

        // max/min_length
        //  - inclusive range boundaries of valid iid text representation length

        static constexpr auto min_length = 2 * sizeof (nonce) + 1;
        static constexpr auto max_length = 2 * sizeof (nonce) + 2 * sizeof (timestamp);
    };

    // translate
    //  - for passing IID as a log function parameter
    //
    inline std::wstring translate (const iid & identity, const std::wstring &) {
        return identity.serialize ();
    }

    // general comparison operators

    inline bool operator == (const iid & a, const iid & b) {
        return a.timestamp == b.timestamp
            && a.nonce == b.nonce;
    }
    inline bool operator != (const iid & a, const iid & b) {
        return a.timestamp != b.timestamp
            || a.nonce != b.nonce;
    }
    inline bool operator < (const iid & a, const iid & b) {
        return a.timestamp < b.timestamp
            || (a.timestamp == b.timestamp && (a.nonce < b.nonce));
    }
    inline bool operator > (const iid & a, const iid & b) {
        return  (b < a);
    }
    inline bool operator <= (const iid & a, const iid & b) {
        return !(b < a);
    }
    inline bool operator >= (const iid & a, const iid & b) {
        return !(a < b);
    }
}

#endif
