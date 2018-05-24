#ifndef RADDI_SUBSCRIPTIONS_H
#define RADDI_SUBSCRIPTIONS_H

#include "../common/lock.h"
#include "raddi_eid.h"
#include <vector>
#include <set>

namespace raddi {

    // subscriptions
    //  - list of channels/threads/streams identified by EID that the peer wishes to receive
    //  - if 'everything' is set, the peer is subscribed to all data except streams,
    //    streams must still be subscribed to
    //
    class subscriptions {
        mutable ::lock              lock;
        std::vector <raddi::eid>    data; // insertion-sorted vector, TODO: consider merging with 'raddi::noticed'
        bool                        everything = false;
    public:
        mutable bool                changed = false; // subscriptions list changed since last successful save

    public:

        // (un)subscribe(to_everything(_unsynchronized))
        //  - maintains storage of subscriptions honoring limits (to avoid DoS by malicious peer)
        //  - for connections it is called by coordinator after it processes raddi::request frames
        //
        void subscribe (const eid &, std::size_t max_individual_subscriptions = (std::size_t) -1);
        void subscribe_to_everything ();
        void unsubscribe (const eid &);

        bool is_subscribed (const eid * begin, const eid * end) const;

        template <std::size_t N>
        bool is_subscribed (const eid (&list) [N]) const {
            return this->is_subscribed (&list [0], &list [N]);
        }

        // enumerate
        //  - calls callback with every 'eid' in data vector
        //
        template <typename F>
        void enumerate (F callback) const {
            immutability guard (this->lock);
            for (const auto & id : this->data) {
                callback (id);
            }
        }

        // load/save
        //  - loads or saves subscription data to file at 'path'
        //  - returns number of entries loaded/saved plus 1, or 0 on failure
        //
        std::size_t load (const std::wstring & path);
        std::size_t save (const std::wstring & path) const;

    private:
        void subscribe_to_everything_unsynchronized ();
    };
}

#endif
