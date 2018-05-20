#include "localhosts.h"

#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <iterator>
#include <vector>

void LocalHosts::refresh () {
    std::set <raddi::address> fresh;
    std::vector <std::uint8_t> buffer;

    ULONG n = 16384;
    ULONG result = 0;
    DWORD flags = GAA_FLAG_SKIP_FRIENDLY_NAME | GAA_FLAG_SKIP_ANYCAST
                | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    do {
        buffer.resize (n);
    } while ((result = GetAdaptersAddresses (AF_UNSPEC, flags, NULL, (IP_ADAPTER_ADDRESSES *) buffer.data (), &n)) == ERROR_BUFFER_OVERFLOW);

    if (result == ERROR_SUCCESS) {
        if (auto p = (IP_ADAPTER_ADDRESSES *) buffer.data ()) {
            do {
                if (auto a = p->FirstUnicastAddress) {
                    do {
                        fresh.emplace (a->Address.lpSockaddr);
                    } while ((a = a->Next) != nullptr);
                }
            } while ((p = p->Next) != nullptr);
        }

        std::set <raddi::address> added;
        std::set <raddi::address> removed;

        {
            exclusive guard (this->lock);
            if (this->addresses.size () != fresh.size ()) {
                std::set_difference (fresh.begin (), fresh.end (), this->addresses.begin (), this->addresses.end (), std::inserter (added, added.end ()));
                std::set_difference (this->addresses.begin (), this->addresses.end (), fresh.begin (), fresh.end (), std::inserter (removed, removed.end ()));

                this->addresses = std::move (fresh);
            }
        }

        for (const auto & a : added) {
            this->report (raddi::log::level::event, 6, a);
        }
        for (const auto & a : removed) {
            this->report (raddi::log::level::event, 7, a);
        }
    } else
        this->report (raddi::log::level::error, 14, raddi::log::api_error (result));
}

bool LocalHosts::contains (raddi::address a) const {
    a.port = 0;
    immutability guard (this->lock);
    return this->addresses.count (a);
}


