#ifndef RADDI_DATABASE_SHARD_H
#define RADDI_DATABASE_SHARD_H

#include "raddi_database.h"
#include "raddi_database_row.h"

// shard
//  - part of table determined by id.timestamp
//
template <typename Key>
class raddi::db::shard
    : log::provider <component::database> {

    friend class table <Key>;
    mutable ::lock  lock;

    std::uint32_t   base; // base timestamp (lowest) of this shard
    std::uint32_t   accessed; // timestamp of last access
    file            index;
    file            content;

    // deleted
    //  - cached number of deleted entries
    //  - updated only when shard is actually opened (or erased from)
    //     - this affects .size()
    //
    std::uint32_t   deleted;

    // cache
    //  - a primary index to the shard's data and positional information
    //  - sorted from oldest to newest
    //
    std::vector <Key> cache;

public:
    shard (std::uint32_t base, const db::table <Key> * = nullptr);
    shard (shard &&);
    shard & operator = (shard &&);

    // close
    //  - frees shard cache and closes file handles
    //
    bool close ();
    bool closed () const { return this->index.closed (); }

    // advance
    //  - loads new data added to shard by different process
    //
    bool advance (const db::table <Key> *);

    // reload
    //  - atomic clear and reload of row cache
    //
    bool reload (const db::table <Key> *);

    // flush
    //  - general flush of data and metadata so that db readers see the changes
    //
    void flush ();

    // insert
    //  - inserts whole entry (decoded protocol frame) into the shard, adding cached 'root' eids
    //
    bool insert (const db::table <Key> *, const entry * data, std::size_t size, const root &, bool & exists);

    // erase
    //  - deletes entry from shard (overwrites with zeros actually)
    //  - if 'thorough' then also content is overwritten
    //  - returns false on error or if no such entry is present
    //
    bool erase (const db::table <Key> *, const decltype (Key::id) & entry, bool thorough);

    // get
    //  - finds 'entry' in this shard, optionally retrieves remaining row details
    //  - returns true if such entry exists, false otherwise
    //
    bool get (const db::table <Key> *, const decltype (Key::id) & entry, Key * = nullptr);

    // get
    //  - finds 'entry' in this shard, reconstructs it for transmission
    //  - what - determines which parts of entry are actually gathered
    //  - buffer - (sizeof (raddi::entry) + raddi::entry::max_content_size) bytes of space needed
    //  - length - receives actual size of the complete entry
    //  - demand - if nonzero, only 'demand' bytes of 'content' is written into 'buffer'
    //  - returns true if such entry exists, false otherwise
    //
    bool get (const db::table <Key> *, const decltype (Key::id) & entry, read what, void * buffer, std::size_t * length, std::size_t demand = 0u);

    // get
    //  - returns 'index'-th entry in this shard, reconstructs it for transmission
    //  - what - determines which parts of entry are actually gathered
    //  - buffer - (sizeof (raddi::entry) + raddi::entry::max_content_size) bytes of space needed
    //  - length - receives actual size of the complete entry
    //  - demand - if nonzero, only 'demand' bytes of 'content' is written into 'buffer'
    //  - returns true if index is valid, false otherwise
    //
    bool get (const db::table <Key> *, std::size_t index, read what, void * buffer, std::size_t * length, std::size_t demand = 0u);

    // size
    //  - returns number of rows in the shard
    //  - reliable only for 'writer' as the actual size may change immediately after the call
    //  - substracts .deleted counter which is updated only when the shard is actually opened
    //
    std::size_t size (const db::table <Key> *) const;

    // top
    //  - retrieves key of latest (youngest) inserted entry (parameter)
    //  - returns false if shard is closed or empty
    //
    bool top (Key *) const;

    // split
    //  - splits a new shard containing entries as old as 'timestamp' or older
    //  - resulting shard can be empty
    //
    shard split (const db::table <Key> *, std::uint32_t timestamp);

    // enumerate
    //  - enumerates entries, calls callback with every 'row' that match
    //  - callback signature must be compatible with: bool f (const Key &, std::uint8_t *);
    //     - first called with second argument nullptr, if returns true, second call provides data
    //
    template <typename F>
    void enumerate (const db::table <Key> * table, F callback);

public:
    friend bool operator < (const shard & a, const shard & b) { return a.base < b.base; }
    friend bool operator < (const shard & a, const std::uint32_t & b) { return a.base < b; }
    friend bool operator < (const std::uint32_t & a, const shard & b) { return a < b.base; }

private:
    std::wstring path (const db::table <Key> *, const wchar_t * suffix = L"") const;

    void unsynchronized_close ();
    bool unsynchronized_advance (const db::table <Key> *);
    void unsynchronized_insert_to_cache (const Key &);

    bool unsynchronized_get (const db::table <Key> *, const decltype (Key::id) &, Key * = nullptr,
                             read = read::nothing, void * = nullptr, std::size_t * = nullptr, std::size_t = 0u);
    bool unsynchronized_read (const db::table <Key> *, typename std::vector <Key>::const_iterator i,
                              read = read::nothing, void * = nullptr, std::size_t = 0u);
    bool unsynchronized_insert (const db::table <Key> *, const entry * data, std::size_t size, const root &);
};

#include "raddi_database_shard.tcc"
#endif
