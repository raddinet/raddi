#include "raddi_detached.h"
#include "raddi_entry.h"
#include "../common/file.h"
#include <algorithm>
#include <set>

void raddi::detached::insert (const eid & parent, const entry * entry, std::size_t size) {
    auto data = reinterpret_cast <const std::uint8_t *> (entry);

    exclusive guard (this->lock);
    this->data [parent.timestamp].emplace (parent.identity, std::vector <std::uint8_t> (data, data + size));
}

std::size_t raddi::detached::reject (const eid & parent) {
    exclusive guard (this->lock);
    return this->unsynchronized_recursive_erase (parent);
}

std::size_t raddi::detached::unsynchronized_recursive_erase (const eid & parent, unsigned int iteration) {

    // TODO: improve protection from endless loops caused by malformed (cycling) data
    //  - get local ptr address and compare to GetCurrentThreadStackLimits (); (Win8) or get base from TEB and size from PE header

    if (iteration > 2048)
        return 0;

    auto mm = this->data.find (parent.timestamp);
    if (mm != this->data.end ()) {
        
        std::set <eid> grandparents;

        auto range = mm->second.equal_range (parent.identity);
        for (auto i = range.first; i != range.second; ++i) {
            grandparents.insert (reinterpret_cast <const entry *> (i->second.data ())->parent);
        }

        mm->second.erase (range.first, range.second);

        auto n = grandparents.size ();
        for (const auto & grandparent : grandparents) {
            n += this->unsynchronized_recursive_erase (grandparent, iteration + 1);
        }
        return n;
    } else
        return 0;
}

void raddi::detached::clean (std::uint32_t age) {
    exclusive guard (this->lock);
    this->data.erase (this->data.begin (), this->data.lower_bound (raddi::now () - age)); // TODO: raddi::older?
}

std::size_t raddi::detached::size () const {
    immutability guard (this->lock);

    std::size_t n = 0;
    for (const auto & m : this->data) {
        n += m.second.size ();
    }
    return n;
}

std::size_t raddi::detached::memory () const {
    immutability guard (this->lock);
    
    std::size_t n = sizeof this->data;
    for (const auto & m : this->data) {
        n += sizeof m;
        for (const auto & mm : m.second) {
            n += sizeof mm;
            n += mm.second.size ();
        }
    }
    return n;
}
