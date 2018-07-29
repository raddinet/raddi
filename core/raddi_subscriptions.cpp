#include "raddi_subscriptions.h"
#include "../common/file.h"
#include <algorithm>

void raddi::subscriptions::subscribe (const eid & subscription, std::size_t max_individual_subscriptions) {
    exclusive guard (this->lock);

    if (this->everything == false) {
        if (this->data.size () < max_individual_subscriptions) {

            if (this->data.empty ()) {
                this->data.reserve (std::min (max_individual_subscriptions, std::size_t (256)));
            }
            this->data.insert (std::lower_bound (this->data.begin (), this->data.end (), subscription), subscription);
            this->changed = true;
        } else {
            this->subscribe_to_everything_unsynchronized ();
        }
    }

    // TODO: stream support: stream EIDs need to be added always, if still congested, simply delete oldest
}

bool raddi::subscriptions::unsubscribe (const eid & subscription) {
    if (!this->everything) {
        exclusive guard (this->lock);

        auto ie = this->data.end ();
        auto ii = std::lower_bound (this->data.begin (), ie, subscription);
        if ((ii != ie) && (*ii == subscription)) {
            this->data.erase (ii);
            this->changed = true;
            return true;
        }
    }
    return false;
}

void raddi::subscriptions::subscribe_to_everything () {
    if (!this->everything) {
        exclusive guard (this->lock);
        this->subscribe_to_everything_unsynchronized ();
    }
}

void raddi::subscriptions::subscribe_to_everything_unsynchronized () {
    this->everything = true;
    this->changed = true;
    this->data.clear ();
    this->data.shrink_to_fit ();
}

bool raddi::subscriptions::is_subscribed (const eid * i, const eid * end) const {
    if (this->everything)
        return true;

    immutability guard (this->lock);

    auto ib = this->data.begin ();
    auto ie = this->data.end ();

    for (; i != end; ++i) {
        auto ii = std::lower_bound (ib, ie, *i);
        if ((ii != ie) && (*ii == *i))
            return true;
    }
    return false;
}

std::size_t raddi::subscriptions::load (const std::wstring & path) {
    exclusive guard (this->lock);

    file f;
    if (f.open (path, file::mode::always, file::access::read, file::share::read, file::buffer::sequential)) {

        this->data.resize (f.size () / sizeof (eid));
        if (f.read (this->data.data (), this->data.size () * sizeof (eid))) {

            std::sort (this->data.begin (), this->data.end ());
            return this->data.size () + 1;
        }
    }
    return 0;
}

std::size_t raddi::subscriptions::save (const std::wstring & path) const {
    immutability guard (this->lock);

    file f;
    if (f.create (path)) {
        if (f.write (this->data.data (), this->data.size () * sizeof (eid))) {
            this->changed = false;
            return this->data.size () + 1;
        }
    }
    return 0;
}
