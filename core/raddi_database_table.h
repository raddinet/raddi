#ifndef RADDI_DATABASE_TABLE_H
#define RADDI_DATABASE_TABLE_H

#include "raddi_database.h"
#include "raddi_database_row.h"
#include <set>
#include <map>

// table
//  - database table, set of shards
//  - NOTE: shards storage (shard.base) is not ready for timestamp wrap-around
//  - performance consideration: 64 posts and 320 votes per second ...top rate is new file per 1 minute
//  - TODO: Monitor::process is not implemented yet!!!
//
template <typename Key>
class raddi::db::table
    : public Monitor <raddi::component::database>
    , virtual log::provider <component::database> {

    mutable ::lock                   lock;   // protects shards
    mutable std::vector <shard <Key>> shards; // sorted vector

    mutable ::lock                   advance_lock;
    mutable std::set <std::uint32_t> advance_marks;

public:
    const raddi::db &  db;
    const std::wstring name;

    // notification_callback_context
    //  - user-sepcified pointer passed to following callbacks
    //
    void * notification_callback_context = nullptr;

    // reader_change_notification_callback
    //  - if set, called by Monitor::process after the changes in the files have been processed
    //
    void (*reader_change_notification_callback) (void *) = nullptr;

public:
    table (const std::wstring & name, const raddi::db & db)
        : Monitor (db.path + L"\\" + name + L"\\")
        , provider ("table", name)
        , db (db)
        , name (name) {}

    // start
    //  - used by 'reading' database connection to monitor for table changes
    //
    using Monitor <raddi::component::database>::start;

    // reload
    //  - attempts to reload index of shards from directory contents
    //
    bool reload ();

    // flush
    //  - general flush of data and metadata so that db readers see the changes
    //
    void flush ();

    // prune/optimize
    //  - frees memory and closes oldest shards
    //     - prune closes shards exceeding the 'keep' count
    //     - optimize closes shards not accessed since 'threshold'
    //  - returns number of shards actually closed
    //
    std::size_t prune (std::size_t keep);
    std::size_t optimize (std::uint32_t threshold);

    // insert
    //  - inserts whole entry (decoded protocol frame) into the table, copying parent thread
    //  - if parent entry is missing from the table the insertion fails
    //     - i.e. the node didn't see channel/thread begin (yet) and will request the data later manually
    //
    bool insert (const entry * entry, std::size_t size, const root &, bool & exists);

    // erase
    //  - erases specified entry from the table
    //  - if 'thorough' then also content is overwritten by zeros
    //  - returns false on error or if no such entry is present
    //
    bool erase (const decltype (Key::id) & entry, bool thorough = false);

    // get
    //  - finds 'entry' in this table, optionally retrieves remaining row details
    //  - returns true if such entry exists, false otherwise
    //
    bool get (const decltype (Key::id) & entry, Key * = nullptr) const;

    // get
    //  - finds 'entry' in this table, reconstruct full entry for transmission
    //  - what - determines which parts of entry are actually gathered
    //  - buffer - (sizeof (raddi::entry) + raddi::entry::max_content_size) bytes of space needed
    //  - length - receives actual size of the complete entry
    //  - demand - if nonzero, only 'demand' number bytes of 'content' is written into 'buffer'
    //  - returns true if such entry exists, false otherwise
    //
    bool get (const decltype (Key::id) & entry, read what, void * buffer, std::size_t * length = nullptr, std::size_t demand = 0u) const;
    bool get (const decltype (Key::id) & entry, void * buffer, std::size_t * length = nullptr, std::size_t demand = 0u) const {
        return this->get (entry, read::everything, buffer, length, demand);
    }

    // top
    //  - retrieves key of latest (youngest) inserted entry (parameter)
    //  - returns true if provided, false if table is empty
    //
    bool top (Key *) const;

    // empty
    //  -
    //
    bool empty () const {
        return this->shards.empty ();
    }

    // stats
    //  - computes some basic resource demands and statistics for the table
    //
    statistics stats () const;

    // get_shard_sizes
    //  - 'callback' gets called for every shard, the parameters are: base timestamp and shard size
    //     - returning false cancels the enumeration
    //  - signature must be compatible with: bool (std::uint32_t, std::size_t)
    //
    template <typename T>
    void enumerate_shard_info (T callback) const;

    // select
    //  - calls 'callback' for entries within provided range that both 'constain' and 'query'
    //    returns true on
    //  - range 'oldest' - 'latest' is inclusive, entries having those timestamps are returned
    //  - returns number of entries found within the range for which 'constrain' returned true
    //  - signatures:
    //      - bool constrain (const Key &, const auto &);
    //      - bool query (const Key &, const auto &);
    //      - void callback (const Key &, const auto &, std::uint8_t *);
    //  - unnamed structure members:
    //      - std::uint32_t shard; - shard identitier, base (lowest) timestamp
    //      - std::uint32_t index; - row index in current shard
    //      - std::size_t   count; - row count in current shard
    //      - std::size_t   total; - total evaluated entries in shards
    //      - std::size_t   match; - total rows matching timestamp range and 'constrain'
    //
    template <typename T, typename U, typename V>
    std::size_t select (std::uint32_t oldest, std::uint32_t latest,
                        T constrain, U query, V callback) const;

    // select
    //  - calls 'callback' with full entry data for every entry in range
    //  - callback signature must be compatible with: void f (const Key &, std::uint8_t *);
    //  - returns number of entries found
    //
    template <typename V>
    std::size_t select (std::uint32_t oldest, std::uint32_t latest,
                        V callback) const {
        return this->select (oldest, latest,
                             [] (const Key &, const auto & detail) { return true; },
                             [] (const Key &, const auto & detail) { return true; },
                             callback);
    }

    // count
    //  - calls select to count number of entries within the range
    //
    std::size_t count (std::uint32_t oldest, std::uint32_t latest) {
        return this->select (oldest, latest,
                             [] (const Key &, const auto & detail) { return true; },
                             [] (const Key &, const auto & detail) { return false; },
                             [] (const Key &, const auto & detail, std::uint8_t *) {});
    }

    // TODO: queries that will be needed later
    //  - select all in thread
    //  - select all descending some parent entry
    //  - select by identity (posts by someone) or by parent identity (all replies to someone)

private:
    table (const table &) = delete;
    table & operator = (const table &) = delete;

    // unsynchronized_internal_find/get_shard
    //  - requires outside synchronization as 'get' also adds shards
    //
    shard <Key> * unsynchronized_find_shard (std::uint32_t timestamp) const;
    shard <Key> * unsynchronized_get_shard (std::uint32_t timestamp);

    bool need_shard_to_advance (const shard <Key> *) const;

    virtual bool process (const std::wstring & filename) override;
    virtual std::wstring render_directory_path () const override {
        return this->db.table_directory_path (this->name);
    }
};

#include "raddi_database_table.tcc"
#endif
