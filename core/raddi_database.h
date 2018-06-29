#ifndef RADDI_DATABASE_H
#define RADDI_DATABASE_H

#include "raddi_entry.h"
#include "raddi_peer_levels.h"

#include "../common/log.h"
#include "../common/file.h"
#include "../common/lock.h"
#include "../common/monitor.h"

#include <string>
#include <vector>
#include <memory>

namespace raddi {

    // db
    //  - fast direct data storage with sharding for performance
    //
    class db
        : log::provider <component::database> {

    public:
        // root
        //  - top level entry references stored for fast search
        //  - for channel/identity announcement entries both equal to 'parent'
        // 
        struct root {
            eid channel;
            eid thread;
        };

        // settings
        //  - TODO: values roughly chosen, needs to undergo major tuning
        //
        struct {
#ifdef NDEBUG
            bool reinsertion_validation = false;
#else
            bool reinsertion_validation = true;
#endif

            // store_everything
            //  - have database insert all valid received entries, don't consider subscriptions
            //  - this is a mode for core network nodes
            //
            bool store_everything = false;

            // backtrack_granularity (seconds)
            //  - TODO: raddi_defaults.h
            //
            unsigned int backtrack_granularity = 4 * 86400;

            // forward_granularity (seconds)
            //  - 
            //
            unsigned int forward_granularity = 86400;

            // synchronization_threshold
            //  - age of entries to normally request from a channel if we don't have anything,
            //    i.e. when subscribing to the channel
            //  - must not exceed 67108860 (but that's already unrealistically large)
            //
            unsigned int synchronization_threshold = 62 * 86400;
            unsigned int synchronization_base_offset = 3600; // 1 hour
            unsigned int synchronization_everything_base_offset = 300; // 5 minutes

            // minimum_shard_size (entries)
            //  - number of rows in the latest shard, before creating a new one is considered
            //
            unsigned int minimum_shard_size = 2; // 2048;

            // maximum_shard_size (entries)
            //  - number of rows, hard limit, attempt to insert will make the table to split the shard
            //  - tune carefully, shards are searched linearly, and re-sorted on insertion!
            //     - do not exceed 65536, that could overflow bit-field optimizations in crow/irow
            //
            unsigned int maximum_shard_size = 8192;

            // shard_trimming_threshold
            //  - how long will a shard stay in memory after last access
            //  - default is 20 minutes now, using 0 will disable trimming
            //
            unsigned int shard_trimming_threshold = 1200;

            // minimum_active_shards
            //  - do not optimize memory usage below this limit, 0 disables this
            //
            unsigned int minimum_active_shards = 24;

            // maximum_active_shards
            //  - max db shards loaded at a time, each uses 2 OS handles
            //  - the actual maximum number of active shards can exceed this value
            //    briefly in cases of heavy utilization, 0 disables the limit
            //
            unsigned int maximum_active_shards = 768;

            // disk_flush_interval
            //  - 
            //
            unsigned int disk_flush_interval = 4000; // 4s

        } settings;

        // statistics
        //  - general info about the database
        //
        struct statistics {
            std::size_t rows = 0;
            struct {
                std::size_t total = 0;
                std::size_t active = 0;
            } shards;

            void operator += (const statistics & other) {
                this->rows += other.rows;
                this->shards.total += other.shards.total;
                this->shards.active += other.shards.active;
            }
        };

        const file::access mode = file::access::read;
        const std::wstring path;

    public:

        // db constructor
        //  - access: global file access mode for this instance
        //            there can be only one writer and any number of reades per directoty (locked through .lock file)
        //
        db (file::access, const std::wstring & path);
        ~db ();

        // connected
        //  - db is connected if lock file is opened, otherwise error was already reported
        //
        bool connected () const { return !this->lock.closed (); }

        // stats
        //  - computes some basic resource demands and statistics
        //
        statistics stats () const;

        // flush
        //  - flushes OS buffers of all open files
        //
        void flush ();

        // optimize
        //  - closes database shards that weren't in use for some time
        //    and all that exceed maximum
        //  - strong optimization will keep only minimum shards open
        //    (used when low on memory)
        //
        void optimize (bool strong = false);

        // assess/assessment
        //  - verifies proof and signature entry against identity in database
        //  - root is not provided for 'rejected' result or  if the database is not connected
        //
        enum assessment {
            rejected = 0, // invalid, don't insert to the database
            detached = 1, // valid, but database misses parent (unsubmitted perhaps), can't insert
            classify = 2, // valid, insert at your discretion
            required = 3, // required, insert if possible
        };
        assessment assess (const void * data, std::size_t size, root *);

        // insert
        //  - 
        //
        bool insert (const void * data, std::size_t size, const root &, bool & exists);

        // erase
        //  - 
        //
        bool erase (const eid & entry, bool thorough = false);

        bool get (const eid & entry, void * buffer, std::size_t * length) const;

        // TODO: add symmetric cipher (or just XOR) for data files and store it alongside of them, this is just to prevent raw drive search

        // typical use scenarios
        //  - search identities
        //  - search channels
        //  - list latest channels
        //  - list latest threads in a channel
        //  - list whole tree belonging to a thread
        //  - list all posts/etc by some identity

        // shard_instance_name
        //  - implemented in ...shard.cpp
        //
        std::wstring shard_instance_name (std::uint32_t base, const std::wstring & table) const;

        // table_directory_path
        //  - implemented in ...table.cpp
        //
        std::wstring table_directory_path (const std::wstring & table) const;

    private:
        file lock;

        // following internal classes are defined in their own headers

        enum class read : unsigned int; // raddi_database_row.h

        // rows
        //  - raddi_database_row.h
        //  - more compact, prefixed, rows for special tables

        struct row;
        struct trow; // threads index row
        struct crow; // channels index row
        struct irow; // identities index row

        // shards
        //  - raddi_database_shard.h

        template <typename Key>
        class shard;

        // tables
        //  - raddi_database_table.h

        template <typename Key>
        class table;

    public:

        // peers
        //  - raddi_database_peerset.h

        class peerset;

        // tables
        //  - data - all data that doesn not belong in any other table
        //  - threads - root thread entries
        //  - channels - root channel entries and directly following meta entries
        //  - identities - root identity entries and directly following meta entries
        //  - NOTE: sent entries and private keys are stored by client apps, not nodes

        // TODO? table changes; // changes in identity/channel descriptions and sidebar info
        // TODO? table online; // cache of special channel for identities participating in chat

        std::unique_ptr <table <row>> data;
        std::unique_ptr <table <trow>> threads;
        std::unique_ptr <table <crow>> channels;
        std::unique_ptr <table <irow>> identities;

        std::unique_ptr <peerset> peers [levels];
    };
}

#include "raddi_database.tcc"
#endif
