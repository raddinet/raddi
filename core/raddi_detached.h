#ifndef RADDI_DETACHED_H
#define RADDI_DETACHED_H

#include "../common/lock.h"
#include "../common/counter.h"
#include "raddi_eid.h"

#include <vector>
#include <map>

namespace raddi {
    struct entry;

    // detached
    //  - special cache for reordering entries that arrived before their parent entries
    //  - optimized for efficiently dropping too old entries
    //
    class detached {
        mutable ::lock lock;

        // data
        //  - key - timestamp of entry's PARENT's id
        //  - value - key - identity id of entry's PARENT
        //          - value - the entry to be inserted when parent arrives
        //
        std::map <std::uint32_t, std::multimap <raddi::iid, std::vector <std::uint8_t>>> data;

    public:
        counter inserted;
        counter rejected; // only actively rejected, data dropped by 'clean' = 'inserted' - 'processed' - 'rejected'
        counter processed;
        counter highwater;
        std::uint32_t highwater_time = 0;

    public:

        // insert
        //  - adds entry to detached cache
        //
        void insert (const eid & parent, const entry * data, std::size_t size);

        // reject
        //  - erases also all entries whose 'parent' is ID of an entry being erased
        //  - returns number of entries erased
        //
        std::size_t reject (const eid & parent);

        // accept
        //  - invokes 'callback' on every entry which has 'parent' as parent EID
        //  - signature must be compatible with: void callback (std::uint8_t *, std::size_t)
        //
        template <typename Callback>
        bool accept (const eid & parent, Callback callback) {
            this->lock.acquire_exclusive ();

            auto mm = this->data.find (parent.timestamp);
            if (mm != this->data.end ()) {
                try {
                    auto range = mm->second.equal_range (parent.identity);

                    std::vector <std::vector <std::uint8_t>> entries;
                    entries.reserve (std::distance (range.first, range.second));

                    for (auto i = range.first; i != range.second; ++i) {
                        entries.emplace_back (std::move (i->second));
                    }

                    mm->second.erase (range.first, range.second);
                    this->lock.release_exclusive ();

                    for (const auto & e : entries) {
                        if (callback (e.data (), e.size ())) {
                            this->processed += e.size ();
                        } else
                            return false;
                    }
                } catch (...) {
                    this->lock.release_exclusive ();
                    throw;
                }
            } else {
                this->lock.release_exclusive ();
            }
            return true;
        }

        // clean
        //  - deletes all entries older than 'age'
        //
        void clean (std::uint32_t age);

        // size
        //  - returns number of entries currently held and amount of memory used by entries and structures that hold them
        //  - does not attempt to estimate allocation overhead
        //
        counter size () const {
            immutability guard (this->lock);
            return this->unsynchronized_size ();
        }

    private:
        std::size_t unsynchronized_recursive_erase (const eid & parent, unsigned int iteration = 0);
        counter unsynchronized_size () const;
    };
}

#endif
