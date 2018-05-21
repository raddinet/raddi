#ifndef RADDI_EID_H
#define RADDI_EID_H

#include "raddi_iid.h"

namespace raddi {

    // eid
    //  - entry ID
    // 
    struct eid {
        std::uint32_t timestamp; // entry creation date/time
        raddi::iid    identity;  // entry author's identity

        // eid from iid constructor
        //  - derives creation-announcing eid out of provided identity
        //    (that is where timestamp equals identity creation time)
        //
        eid (const iid & id)
            : timestamp (id.timestamp)
            , identity (id) {}

        eid () = default;
        eid (eid &&) = default;
        eid (const eid &) = default;
        eid & operator = (eid &&) = default;
        eid & operator = (const eid &) = default;

        // operator iid
        //  - conversion, extracts identity
        //
        explicit operator iid () const {
            return this->identity;
        }

        // serialize
        //  - generates displayable representation of the eid object
        //  - buffer length must be at least 26 characters
        //
        bool serialize (wchar_t * buffer, std::size_t length) const;

        // serialize
        //  - generates std::wstring containing displayable representation of the eid object
        //
        inline std::wstring serialize () const {
            wchar_t buffer [26];
            this->serialize (buffer, sizeof buffer / sizeof buffer [0]);
            return buffer;
        }

        // parse
        //  - attempts to parse serialized eid, ignoring whitespace
        //  - contents of the eid object are undefined on failure
        //  - returns number of characters parsed on success, zero on failure
        //
        std::size_t parse (const wchar_t *);

        // erased
        //  - deletion within database is done by zeroing the record
        //
        inline bool erased () const {
            return this->timestamp == 0
                && this->identity.erased ();
        }
    };

    // translate
    //  - for passing EID as a log function parameter
    //
    inline std::wstring translate (const eid & entry, const std::wstring &) {
        return entry.serialize ();
    }

    // general comparison operators

    inline bool operator == (const eid & a, const eid & b) {
        return a.timestamp == b.timestamp
            && a.identity == b.identity;
    }
    inline bool operator != (const eid & a, const eid & b) {
        return a.timestamp != b.timestamp
            || a.identity != b.identity;
    }
    inline bool operator <  (const eid & a, const eid & b) {
        return a.timestamp < b.timestamp
            || (a.timestamp == b.timestamp && a.identity < b.identity);
    }
    inline bool operator <= (const eid & a, const eid & b) {
        return !(b < a);
    }
    inline bool operator >  (const eid & a, const eid & b) {
        return  (b < a);
    };
    inline bool operator >= (const eid & a, const eid & b) {
        return !(a < b);
    };
}

#endif
