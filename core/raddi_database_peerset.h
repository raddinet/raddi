#ifndef RADDI_DATABASE_PEERSET_H
#define RADDI_DATABASE_PEERSET_H

#include "../common/log.h"
#include "../common/lock.h"

#include "raddi_address.h"
#include "raddi_database.h"

#include <map>

class raddi::db::peerset
    : log::provider <component::database> {

    mutable ::lock lock;
    mutable bool ipv4changed = false;
    mutable bool ipv6changed = false;

    std::map <short, std::wstring> paths;

    // addresses
    //  - 
    //  - blacklisted inbound addresses have port number set to 0
    //  - std::uint16_t is assessment
    //     - generally value 0 - 255
    //     - for blacklisted nodes it's (timestamp / 86400) when the ban gets lifted
    //
    std::map <address, std::uint16_t> addresses;

public:
    // new_record_assessment
    //  - 
    //
    static constexpr std::uint16_t new_record_assessment = 0x40;

    peerset (level l)
        : provider ("peers", translate (l)) {};

    void insert (const address &, std::uint16_t = new_record_assessment);
    void erase (const address &);

    void load (const std::wstring & path, int family, int level);

    void save () const;
    void save (int family) const;

    std::uint32_t adjust (const address &, std::int16_t adj);
    address       select (std::size_t random_value, std::uint16_t * assessment = nullptr);

    void prune (std::uint16_t threshold = 0);
    bool empty () const;
    bool count (const address & a) const;
    bool count_ip (const address & a) const;
};

#endif
