#ifndef RADDI_TIMESTAMP_H
#define RADDI_TIMESTAMP_H

#include <ctime>
#include <cstdint>

namespace raddi {

    // timestamp
    //  - returns raddi timestamp (current or by parameter)
    //  - number of seconds since January 1, 2018 UTC
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
    //  - number of microseconds since January 1, 2018 UTC
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
    bool older (std::uint32_t timestamp, std::uint32_t reference);
}

#endif
