#ifndef RADDI_TIMESTAMP_H
#define RADDI_TIMESTAMP_H

#include <ctime>
#include <cstdint>

namespace raddi {

    // timestamp_base
    //  - difference of 1.1.2020 00:00:00 - January 1, 1601 UTC in seconds (unix timestamp minus 0x5e0be100)
    //
    static constexpr auto timestamp_base = 0x3141c7200uLL;

    // timestamp
    //  - returns raddi timestamp (current or by parameter)
    //  - number of seconds since January 1, 2020 UTC
    //
    std::uint32_t timestamp ();
    std::uint32_t timestamp (const std::tm &);

#ifdef WINAPI
    std::uint32_t timestamp (const SYSTEMTIME &);
#endif

    static inline std::uint32_t now () {
        return raddi::timestamp ();
    };

    // microtimestamp
    //  - returns raddi timestamp in resolution of microseconds
    //  - number of microseconds since January 1, 2020 UTC
    //
    std::uint64_t microtimestamp ();

    // time
    //  - returns UTC specified by the timestamp
    // 
    std::tm time (std::uint32_t timestamp);

#ifdef WINAPI
    // wintime
    //  - returns Windows' SYSTEMTIME for timestamp
    //
    SYSTEMTIME wintime (std::uint32_t timestamp);
    SYSTEMTIME wintime (std::uint64_t timestamp);
#endif

    // older
    //  - returns true if 'timestamp' is older than 'reference'
    //    taking potential integer overflow into account
    //
    inline bool older (std::uint32_t timestamp, std::uint32_t reference) {
        return (timestamp - reference) > 0x8000'0000u;
    }
    inline bool older (std::uint64_t microtimestamp, std::uint64_t reference) {
        return (microtimestamp - reference) > 0x8000'0000'0000'0000uLL;
    }
}

#endif
