#ifndef RADDI_DATABASE_TABLE_TCC
#define RADDI_DATABASE_TABLE_TCC

#include "../common/directory.h"

template <typename Key>
bool raddi::db::table <Key>::reload () {
    this->shards.reserve (16384 / sizeof (shard <Key>));

    switch (directory::create (this->db.table_directory_path (this->name).c_str ())) {

        case directory::created:
            return true;

        case directory::already_exists:
            try {
                std::vector <std::uint32_t> timestamps;
                timestamps.reserve (4096);

                auto callback = [&timestamps] (const wchar_t * filename) {
                    wchar_t * tail = nullptr;
                    auto timestamp = std::wcstoul (filename, &tail, 16);
                    if ((tail == &filename [8]) && (filename [8] == L'\0')) {
                        timestamps.push_back (timestamp);
                    }
                };

                if (::directory ((this->db.table_directory_path (this->name) + L"????????").c_str ()) (callback)) {
                    std::sort (timestamps.begin (), timestamps.end ());

                    exclusive guard (this->lock);

                    this->shards.clear ();
                    this->shards.reserve (timestamps.size ());
                    for (const auto timestamp : timestamps) {
                        this->shards.emplace_back (timestamp, this);
                    }
                    return true;
                }
            } catch (const std::bad_alloc &) {
                // return false
            }
    }
    return false;
}

template <typename Key>
void raddi::db::table <Key>::flush () {
    immutability guard (this->lock);
    for (auto & shard : this->shards) {
        if (!shard.closed ()) {
            shard.flush ();
        }
    }
}

template <typename Key>
bool raddi::db::table <Key>::insert (const entry * entry, std::size_t size, const root & top, bool & exists) {
    try {
        exclusive guard (this->lock);
        return this->unsynchronized_get_shard (entry->id.timestamp)->insert (this, entry, size, top, exists);
    } catch (const std::bad_alloc &) {
        exists = false;
        return false;
    }
}

template <typename Key>
bool raddi::db::table <Key>::erase (const decltype (Key::id) & entry, bool thorough) {
    immutability guard (this->lock);
    if (auto shard = this->unsynchronized_find_shard (entry.timestamp)) {
        return shard->erase (this, entry, thorough);
    } else
        return false;
}

template <typename Key>
bool raddi::db::table <Key>::get (const decltype (Key::id) & entry, Key * r) const {
    immutability guard (this->lock);
    if (auto shard = this->unsynchronized_find_shard (entry.timestamp)) {
        if (this->need_shard_to_advance (shard)) {
            shard->advance (this);
        }
        return shard->get (this, entry, r);
    } else
        return false;
}

template <typename Key>
bool raddi::db::table <Key>::get (const decltype (Key::id) & entry, read what,
                                  void * buffer, std::size_t * length, std::size_t demand) const {
    immutability guard (this->lock);
    if (auto shard = this->unsynchronized_find_shard (entry.timestamp)) {
        if (this->need_shard_to_advance (shard)) {
            shard->advance (this);
        }
        return shard->get (this, entry, what, buffer, length, demand);
    } else
        return false;
}

template <typename Key>
bool raddi::db::table <Key>::top (Key * row) const {
    immutability guard (this->lock);

    // looping through shards to find top because shard may be empty due to deletions

    auto i = this->shards.rbegin ();
    auto e = this->shards.rend ();
    
    while (i != e) {
        auto shard = &*i;

        if (this->need_shard_to_advance (shard)) {
            shard->advance (this);
        }
        if (shard->top (row))
            return true;

        ++i;
    }
    return false;
}

template <typename Key>
bool raddi::db::table <Key>::need_shard_to_advance (const shard <Key> * s) const {
    if (s->closed ())
        return true;

    exclusive guard (this->advance_lock);
    return this->advance_marks.erase (s->base);
}

template <typename Key>
raddi::db::shard <Key> * raddi::db::table <Key>::unsynchronized_get_shard (std::uint32_t timestamp) {

    // new shard on empty table or data past forward granularity

    if (this->shards.empty () || (timestamp >= (this->shards.back ().base + this->db.settings.forward_granularity))) {
        this->shards.emplace_back (timestamp, this);
        return &this->shards.back ();
    }

    // new shard on data older than backtrack granularity

    if (timestamp < this->shards.front ().base) {
        auto ts = this->shards.front ().base;
        while (ts > timestamp) {
            ts -= this->db.settings.backtrack_granularity;
        }
        return &*this->shards.emplace (this->shards.begin (), ts, this);
    }

    // existing shard
    //  - optimized for common case of inserting to the newest shard
    //  - otherwise binary search for shard with the timestamp
    //     - using inert temporary shard object
    //     - no need to check against 'end', border cases handled above

    typename std::vector <raddi::db::shard <Key>> ::iterator i;
    if (timestamp >= this->shards.back ().base) {
        i = this->shards.begin () + (this->shards.size () - 1);

        // if inserting timestamp highest than the largest already in this latest shard
        // and that shard would be likely split soon, then create new shard

        if ((i->cache.size () >= this->db.settings.minimum_shard_size) && (timestamp > i->cache.crbegin ()->id.timestamp)) {
            this->shards.emplace_back (timestamp, this);
            return &this->shards.back ();
        }

    } else {
        i = std::upper_bound (this->shards.begin (), this->shards.end (), timestamp) - 1;
    }

    // split shard if size reaches or exceeds set maximum
    // TODO: use middle value, not average, so that we don't end up with empty shard

    if (i->cache.size () >= this->db.settings.maximum_shard_size) {

        auto divider = 0uLL;
        auto next = i + 1;

        if (next == this->shards.end ()) {
            std::uint64_t ts = 0;
            for (const auto & row : i->cache) {
                ts += row.id.timestamp;
            }
            divider = ts / i->cache.size () + 1;
        } else {
            divider = i->base + (next->base - i->base - 1) / 2 + 1;
        }

        if (i->base < divider) {
            try {
                // split throws int (API) or bad_alloc (caught above)
                return &*this->shards.insert (next, i->split (this, (std::uint32_t) divider));
            } catch (int) {
                // split failed (API), already reported, continue
            }
        }
    }// */
    return &*i;

}

template <typename Key>
raddi::db::shard <Key> * raddi::db::table <Key>::unsynchronized_find_shard (std::uint32_t timestamp) const {

    // fast typical cases
    //  - no shards in table OR adding latest data

    if (this->shards.empty ()) {
        return nullptr;
    }
    if (timestamp >= this->shards.back ().base) {
        return &this->shards.back ();
    }

    // binary search for shard that could contain the timestamp

    auto i = std::upper_bound (this->shards.begin (), this->shards.end (), timestamp);
    if (i == this->shards.begin ())
        return nullptr;
    
    --i;
    return &*i;
}

template <typename Key>
std::size_t raddi::db::table <Key>::optimize (std::uint32_t threshold) {
    immutability guard (this->lock);

    std::size_t n = 0;
    for (auto & s : this->shards) {
        if (raddi::older (s.accessed, threshold)) {
            n += s.close ();
        }
    }
    return n;
}

template <typename Key>
std::size_t raddi::db::table <Key>::prune (std::size_t keep) {
    immutability guard (this->lock);

    const auto size = this->shards.size ();
    const auto tops = size - keep; // max shards to close

    if (keep >= size) {
        // want to keep all and more, close nothing
        return 0;
    }

    std::vector <std::pair <std::uint32_t, shard <Key> *>> index;
    index.reserve (size);

    for (auto & s : this->shards) {
        index.emplace_back (s.accessed, &s);
    }

    std::partial_sort (index.begin (),
                       index.begin () + tops,
                       index.end (),
                       [] (const auto & a, const auto & b) { return raddi::older (a.first, b.first); });

    std::size_t n = 0;
    for (std::size_t i = 0; i != tops; ++i) {
        n += index [i].second->close ();
    }
    return n;
}

template <typename Key>
raddi::db::statistics raddi::db::table <Key>::stats () const {
    raddi::db::statistics s;

    immutability guard (this->lock);
    s.shards.total = this->shards.size ();

    for (const auto & shard : this->shards) {
        immutability shard_guard (shard.lock);
        if (!shard.closed ()) {
            s.rows += shard.cache.size ();
            s.shards.active += 1;
        }
    }
    return s;
}

template <typename Key>
template <typename T>
void raddi::db::table <Key>::enumerate_shard_info (T callback) const {
    immutability guard (this->lock);
    for (const auto & shard : this->shards) {
        if (!callback (shard.base, shard.size (this)))
            break;
    }
}

template <typename Key>
bool raddi::db::table <Key>::process (const std::wstring & filename) {
    exclusive guard (this->lock);

    // exclusive guard (this->advance_lock);
    // this->advance_marks.insert (...);

    // NOTE: This needs to be implemented for any client software to work
    // TODO: handle external changes:
    //    - close all shards reported as new?
    //    - mark referenced shard 'filename' to advance,
    //    - if new then either split happened (reload table) or append happened (???)
    //       - some shard can be pointing to file about to be deleted

    // TODO: this is run on different thread!

    return true;
}

template <typename Key>
    template <typename T, typename U, typename V>
std::size_t raddi::db::table <Key>::select (std::uint32_t oldest, std::uint32_t latest, T constrain, U query, V callback) const {
    struct {
        std::uint32_t shard = 0;
        std::uint32_t index = 0; // row index in current shard
        std::size_t   count = 0; // row count in current shard

        std::size_t   total = 0; // total evaluated entries in shards
        std::size_t   match = 0; // total rows matching timestamp range and constrain
    } info;

    immutability guard (this->lock);
    for (auto & shard : this->shards) {

        if (raddi::older (latest, shard.base)) { // shard.base > latest
            break; // we are done
        }
        if (this->need_shard_to_advance (&shard)) {
            shard.advance (this);
        }

        info.shard = shard.base;
        info.index = 0;
        info.count = shard.size (this);

        shard.enumerate (this, [&info, oldest, latest, constrain, query, callback] (const Key & row, std::uint8_t * data) -> bool {
            bool r = false;
            if (data) {
                callback (row, info, data);
                return false;

            } else {
                if (!raddi::older (row.id.timestamp, oldest) && raddi::older (row.id.timestamp, latest + 1)) {
                    if (constrain (row, info)) {
                        ++info.match;
                        r = query (row, info);
                    }
                }
                ++info.index;
                ++info.total;
                return r;
            }
        });
    }
    return info.match;
}

#endif
