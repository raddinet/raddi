#ifndef RADDI_LOCALHOSTS_H
#define RADDI_LOCALHOSTS_H

#include "../core/raddi_address.h"
#include "../common/lock.h"
#include "../common/log.h"

#include <set>

class LocalHosts
    : raddi::log::provider <raddi::component::server> {

    mutable lock lock;
    std::set <raddi::address> addresses;

public:
    LocalHosts () : provider ("localhosts") {}

    void refresh ();
    bool contains (raddi::address) const;
};

#endif

