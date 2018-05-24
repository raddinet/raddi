#include "raddi_noticed.h"
#include <algorithm>

bool raddi::noticed::insert (const raddi::eid & id) {
    exclusive guard (this->lock);
    return this->data [id.timestamp].insert (id.identity).second;
}

void raddi::noticed::clean (std::uint32_t age) {
    exclusive guard (this->lock);

    auto i = this->data.begin ();
    const auto e = this->data.end ();
    const auto threshold = raddi::now () - age;

    while (i != e) {
        if (raddi::older (i->first, threshold)) {
            i = this->data.erase (i);
        } else {
            ++i;
        }
    }
}
bool raddi::noticed::count (const raddi::eid & id) const {
    immutability guard (this->lock);

    auto i = this->data.find (id.timestamp);
    if (i != this->data.end ())
        return i->second.count (id.identity);
    else
        return false;
}

std::size_t raddi::noticed::size () const {
    immutability guard (this->lock);
    std::size_t n = 0;
    for (const auto & sub : this->data) {
        n += sub.second.size ();
    }
    return n;
}
