#ifndef RADDI_SUBSCRIPTION_SET_H
#define RADDI_SUBSCRIPTION_SET_H

#include "../common/log.h"
#include "../common/uuid.h"
#include "raddi_subscriptions.h"
#include <string>
#include <map>

namespace raddi {

    // subscription_set
    //  - keeps lists of channel/thread EIDs for each client application
    //     - used to store/check both subscriptions and blacklist
    //  - note that for logging purposes this is part of "database" component
    //  - NOTE: this could be optimized further by keeping std::set<raddi::eid> cache
    //          and searching only that one in 'is_subscribed'
    //
    class subscription_set
        : log::provider <component::database> {

        mutable ::lock                  lock;
        std::map <uuid, subscriptions>  data;
        const std::wstring              path;

    public:
        subscription_set (const std::wstring & dbpath, const std::wstring & name)
            : provider ("set", name)
            , path (dbpath + L"\\" + name + L"\\") {}

        // subscribe/unsubscribe
        //  - add or removes EID from subscription set associated with the 'app'
        //  - 'app' is used for file name used to store these details, so use it carefully
        //
        void subscribe (const uuid & app, const eid &);
        bool unsubscribe (const uuid & app, const eid &);

        // is_subscribed
        //  - returns true if any of the app subscriptions contain one of EIDs
        //
        bool is_subscribed (const eid * begin, const eid * end) const;

        template <std::size_t N>
        bool is_subscribed (const eid (&list) [N]) const {
            return this->is_subscribed (&list [0], &list [N]);
        }

        // enumerate
        //  - calls 'callback' with every EID in the set
        //  - used mainly by 'leaf' nodes which tend to have single 'app'
        //    so we probably don't need to take care to remove duplicities
        //    (for now)
        //
        template <typename F>
        void enumerate (F callback) const {
            immutability guard (this->lock);
            for (const auto & subs : this->data) {
                subs.second.enumerate (callback);
            }
        }

        // load
        //  - loads subscription/blacklist data from 'path'
        //
        bool load ();

        // flush
        //  - saves all subscriptions that has changed
        //
        void flush () const;

    private:
        std::wstring member (const uuid & app) const;
    };
}

#endif
