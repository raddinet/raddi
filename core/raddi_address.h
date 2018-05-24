#ifndef RADDI_ADDRESS_H
#define RADDI_ADDRESS_H

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstddef>
#include <string>

#include "../common/log.h"

namespace raddi {

    // address
    //  - simple IPv4/6 abstraction, better than SOCKADDR_INET or SOCKADDR_STORAGE
    //
    struct address {
        std::uint16_t family;
        std::uint16_t port;
        union {
            in_addr   address4;
            in6_addr  address6;
        };

    public:
        address () = default;
        address (const sockaddr *);
#ifdef _WIN32
        address (const SOCKADDR_INET &);
        operator SOCKADDR_INET () const;
#endif
    public:
        bool operator < (const address & other) const;
        bool operator == (const address & other) const;
        bool operator <= (const address & other) const { return !(other < *this); }
        bool operator != (const address & other) const { return !(other == *this); }

    public:
        static std::size_t size (int family);
        static std::wstring name (int family);

        bool valid () const;

        // accessible
        //  - returns true if the address is valid
        //    and internet-accessible (non-intranet)
        //
        bool accessible () const;

        // data/size
        //  - for serialization (without storing the 'family' member)
        //
        std::size_t size () const;
        void *       data ()       { return &this->port; }
        const void * data () const { return &this->port; }
    };

    extern const address loopback_IPv4;
    extern const address loopback_IPv6;

    // translate
    //  - for passing address as a log function parameter
    //  - TODO: delete translate (SOCKADDR_INET) function and fully implement it here
    //
    inline std::wstring translate (raddi::address a, const std::wstring & format) {
        return log::translate ((SOCKADDR_INET) a, format);
        // char buffer[...]
        // a -> buffer
        // return translate (reinterpret_cast <const sockaddr *> (buffer), format);
    }
}

#endif
