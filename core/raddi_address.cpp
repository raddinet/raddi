#include "raddi_address.h"
#include <algorithm>

static const sockaddr_in sin_loopback_IPv4 = { AF_INET, 0, {{127, 0, 0, 1}} };
static const sockaddr_in6 sin_loopback_IPv6 = { AF_INET6, 0, 0, {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1} };

const raddi::address raddi::loopback_IPv4 ((const sockaddr *) &sin_loopback_IPv4);
const raddi::address raddi::loopback_IPv6 ((const sockaddr *) &sin_loopback_IPv6);

std::size_t raddi::address::size (int family) {
    switch (family) {
        case AF_INET:
            return sizeof (address::port) + sizeof (address::address4);
        case AF_INET6:
            return sizeof (address::port) + sizeof (address::address6);
        default:
            return 0;
    }
}
std::wstring raddi::address::name (int family) {
    switch (family) {
        case AF_INET:
            return L"IPv4";
        case AF_INET6:
            return L"IPv6";
        default:
            return L"unknown";
    }
}

std::size_t raddi::address::size () const {
    return raddi::address::size (this->family);
}

bool raddi::address::operator < (const raddi::address & other) const {
    return std::memcmp (this, &other,
                        sizeof (address::family) + std::min (this->size (), other.size ())) < 0;
}
bool raddi::address::operator == (const raddi::address & other) const {
    return std::memcmp (this, &other,
                        sizeof (address::family) + std::min (this->size (), other.size ())) == 0;
}

raddi::address::address (const sockaddr * addr)
    : family (addr->sa_family) {

    switch (this->family) {
        case AF_INET:
            this->port = ntohs (reinterpret_cast <const sockaddr_in *> (addr)->sin_port);
            this->address4 = reinterpret_cast <const sockaddr_in *> (addr)->sin_addr;
            break;
        case AF_INET6:
            this->port = ntohs (reinterpret_cast <const sockaddr_in6 *> (addr)->sin6_port);
            this->address6 = reinterpret_cast <const sockaddr_in6 *> (addr)->sin6_addr;
            break;
    }
}
raddi::address::address (const SOCKADDR_INET & addr)
    : family (addr.si_family) {

    switch (this->family) {
        case AF_INET:
            this->port = ntohs (addr.Ipv4.sin_port);
            this->address4 = addr.Ipv4.sin_addr;
            break;
        case AF_INET6:
            this->port = ntohs (addr.Ipv6.sin6_port);
            this->address6 = addr.Ipv6.sin6_addr;
            break;
    }
}

raddi::address::operator SOCKADDR_INET () const {
    SOCKADDR_INET result;
    switch (this->family) {
        case AF_INET:
            result.Ipv4.sin_family = this->family;
            result.Ipv4.sin_addr = this->address4;
            result.Ipv4.sin_port = htons (this->port);
            std::memset (result.Ipv4.sin_zero, 0, sizeof result.Ipv4.sin_zero);
            break;
        case AF_INET6:
            result.Ipv6.sin6_family = this->family;
            result.Ipv6.sin6_addr = this->address6;
            result.Ipv6.sin6_port = htons (this->port);
            result.Ipv6.sin6_flowinfo = 0;
            result.Ipv6.sin6_scope_id = 0;
            break;
        default:
            result.si_family = this->family;
            break;
    }
    return result;
}

namespace {
    std::uint32_t ubsum (const void * p_, std::size_t n) {
        std::uint32_t a = 0;
        const auto * p = reinterpret_cast <const std::uint8_t *> (p_);
        while (n--) {
            a += *p++;
        }
        return a;
    }
}

bool raddi::address::valid (validation mode) const {
    switch (this->family) {
        case AF_INET:
            return (mode == validation::allow_null_port || this->port != 0)
                && (this->address4.s_addr != 0x00000000)
                && (this->address4.s_addr != 0xFFFFFFFF) // limited broadcast
                && (this->address4.s_addr & 0x00FFFFFF) != 0x00000000 // 0.0.0.0/24
                // documentation reserved
                && (this->address4.s_addr & 0x00FFFFFF) != 0x002000C0 // 192.0.2.0/24
                && (this->address4.s_addr & 0x00FFFFFF) != 0x006433C6 // 198.51.100.0/24
                && (this->address4.s_addr & 0x00FFFFFF) != 0x007100CB // 203.0.113.0/24
                // multicast
                && (this->address4.s_addr & 0x000000F0) != 0x000000E0 // 224.0.0.0/4
                // reserved
                && (this->address4.s_addr & 0x000000F0) != 0x000000F0 // 240.0.0.0/4
                ;
        case AF_INET6:
            auto w0 = htons (this->address6.s6_words [0]);
            return (this->port != 0)
                && (ubsum (&this->address6, sizeof this->address6) != 0)
                && (w0 != 0x0100) // discard
                && (w0 < 0xff00 || w0 > 0xff0f) // multicast
                ;
    }
    return false;
}
bool raddi::address::accessible (validation mode) const {
    if (this->valid (mode))
        switch (this->family) {
            case AF_INET:
                return (this->address4.s_addr & 0x000000FF) != 0x0000007F // 127.0.0.0/8
                    // private networks and loopback
                    && (this->address4.s_addr & 0x000000FF) != 0x0000000A // 10.0.0.0/8
                    && (this->address4.s_addr & 0x0000F0FF) != 0x000010AC // 172.16.0.0/12
                    && (this->address4.s_addr & 0x0000FFFF) != 0x0000A8C0 // 192.168.0.0/16
                    && (this->address4.s_addr & 0x0000FFFF) != 0x0000FEA9 // 169.254.0.0/16
                    ;
            case AF_INET6:
                auto w0 = htons (this->address6.s6_words [0]);
                auto w7 = htons (this->address6.s6_words [7]);
                return (w0 != 0xfe80) // not link-local
                    && (w0 < 0xfc00 || w0 > 0xfdff) // not unique local address range
                    && (ubsum (&this->address6, sizeof this->address6) != 1u || w7 != 0x0001); // not localhost
        }

    return false;
}
