#ifndef RADDI_COORDINATOR_H
#define RADDI_COORDINATOR_H

#include "../node/server.h"
#include "../common/lock.h"
#include "../common/log.h"

#include "raddi_address.h"
#include "raddi_protocol.h"
#include "raddi_timestamp.h"
#include "raddi_discovery.h"
#include "raddi_database.h"
#include "raddi_database_peerset.h"
#include "raddi_subscription_set.h"
#include "raddi_request.h"
#include "raddi_defaults.h"

#include "raddi_detached.h"
#include "raddi_noticed.h"

#include <string>
#include <random>
#include <list>
#include <set>
#include <map>

namespace raddi {
    class connection;
    class coordinator
        : log::provider <component::server> {

        // database
        //  - reference to database instance
        //
        db & database;

        // listeners
        //  - short list of listening sockets accepting connections
        //
        std::list <Listener>   listeners;

        // listening_ports
        //  - cache of listener port numbers for each address family
        //  - not written after initialization, no locking required if it stays that way
        //
        std::map <short, std::set <std::uint16_t>>  listening_ports;

        // connections
        //  - front-inserted to increase performance of reflecting connections detection
        //  - TODO: rename 'connections' to 'network'? again separate 'coordinator'?
        //
        std::list <connection>  connections;

        // discovery
        //  - local peer discovery UDP sockets
        //
        std::list <discovery>   discoverers;

        // random
        //  - used in algorithm selecting peers to connect to and peers announced to others
        //  - the code relies on this being thread safe (I don't lock around this here)
        //
        mutable std::mt19937                                random_generator;
        mutable std::uniform_int_distribution <std::size_t> random_distribution;

        // connect_requests
        //  - addresses to try next, per user request, higher priority
        //
        std::set <address> connect_requests;

    public:

        // subscriptions
        //  - threads/channels subscribed to by client apps
        //  - subscription data (and other) are here, instead in database, because database
        //    is also used by client apps (in order to keep database class minimal)
        //
        raddi::subscription_set subscriptions;

        // blacklist
        //  - for entries manually deleted or blacklisted author/thread/channel
        //
        raddi::subscription_set blacklist;

        // retained
        //  - for entries/threads that are to be kept forever, not automatically deleted
        //  - NOTE that application must add EIDs of the whole chain to keep
        //  - TODO: timely data deletion is not implemented yet, so this is unused for now
        //
        raddi::subscription_set retained;

        // recent
        //  - immediate history of entries propagated through network
        //  - stops broadcast of entries already broadcasted
        //  - erased after raddi::consensus::max_entry_age_allowed
        //  - TODO: move to future raddi::node
        //
        raddi::noticed recent;

        // detached
        //  - insertion cache for reordering detached entries
        //  - TODO: move to future raddi::node
        //
        raddi::detached detached;

        // refused
        //  - cache for EIDs that were refused for some reason
        //  - used to keep it's descendants from 'detached' buffers
        //  - TODO: clean when? how? save/load? -> part of database then, clean after 0x20000000 (or what's in raddi validate)
        //          should save, otherwise 'detached' can get very clogged
        //
        raddi::noticed refused;

    private:
        mutable lock lock;

        // pacing
        //  - last timestamp when coordinator evaluated status
        //
        std::uint32_t pacing = raddi::now ();
        std::uint32_t started = raddi::now ();
        std::uint32_t last_peers_query = raddi::now ();
        std::size_t   previous_secured_count = 0;

        // connect_one_more_announced_node
        //  - when node announcement is received, this bumps the enthusiasm to validate it
        //  - intentionally 'bool' to coalesce multiple announcements
        //
        bool connect_one_more_announced_node = false;

        // core_sync_threshold
        //  - initialized to timestamp which is requested (download) from other core nodes
        //  - on first run (database is empty) it's current time minus database 'synchronization_threshold'
        //    to retrieve all data that all normal nodes will generally care about
        //  - on subsequent starts it's timestamp of newest data we have minus database 'synchronization_base_offset'
        //    for some overlap to account for data potentially lost in transit
        //
        std::uint32_t core_sync_threshold = 0;

        // core_sync_count
        //  - how many other core nodes to ask for full database download
        //    to make sure we really have everything
        //  - TODO: reset when last core node disconnects
        //  - TODO: defaults.h, also read from options?
        // 
        std::uint32_t core_sync_count = 3;

    public:

        // settings
        //  - TODO: not documented much yet
        //  - TODO: move default values to raddi_defaults.h
        //
        struct {
            std::size_t connections = 8; // average number of connections to keep established
            std::size_t max_connections = 1024; // absolute hard maximum, includes inbound, 0 means unlimited
            std::size_t min_connections = 2; // minimum normal connected nodes to allow broadcasting
            std::size_t max_core_connections = 3; // maximum connections to core nodes
            std::size_t min_core_connections = 0; // minimum core nodes to allow broadcasting

            bool local_peers_only = false;
            bool network_propagation_participation = true;
            bool channels_synchronization_participation = true;
            bool full_database_downloads_allowed = false;

            unsigned int keep_alive_period = raddi::defaults::connection_keep_alive_timeout;

            unsigned int announcement_sample_size = 40;
            unsigned int max_requests_per_minute = 4096; // 0 means unlimited
            unsigned int max_allowed_rejected_entries = 16;
            unsigned int max_allowed_unsolicited_entries = 64;
            unsigned int max_individual_subscriptions = 65536; // also streams limit
            unsigned int local_peer_discovery_period = 1200;
            unsigned int more_peers_query_delay = 180;
            unsigned int full_database_download_limit = 62 * 86400;
        } settings;

    public:
        explicit coordinator (db & database);
        void terminate ();

        // process
        //  - called to process requests received from peer connection
        //
        bool process (const unsigned char * request, std::size_t size, connection * peer);

        // incomming
        //  - a 'peer' connection is incomming, accept?
        //
        bool incomming (Socket &&, const sockaddr * peer);

        // established
        //  - connection to peer successfully established and secured
        //  - announces our listening ports to outbound connections
        //
        void established (connection * peer);

        // unavailable
        //  - failed establish connection
        //
        void unavailable (const connection * peer);

        // disagreed
        //  - for various reasons the peers disagreed on valid communication protocol
        //    and it would be detrimental to continue; although we are allowing a few
        //    honest errors to happen before banning the peer
        //
        void disagreed (const connection * peer);

    public:

        // reflecting
        //  - returns true if incomming 'peer' is actually existing local connection out
        //    somehow ending back here AND bans the address for an extended time
        //
        bool reflecting (const raddi::protocol::keyset * peer);

        // reciprocal
        //  - returns true if 'peer' is already connected in opposite sense
        //
        bool reciprocal (const connection * peer);

        // broadcasting
        //  - returns true is the connection into network is sufficient to broadcast data
        //
        bool broadcasting () const;

        // active
        //  - returns total number of secured connections into the network
        //  - if 'attempting' or 'connected' is not null, reports number of connections
        //    of particular levels, being connection attempts or already connected ones
        //
        std::size_t active (std::size_t attempting [levels] = nullptr,
                            std::size_t connected [levels] = nullptr) const;

        // inuse
        //  - returns true if address is connected or being connected to
        //
        bool inuse (const address &) const;

        // blacklisted
        //  - finds if address 'a' is on the blacklist
        //
        bool blacklisted (const address & a) const;

        // empty
        //  - checks if the list of 'level' category of nodes is empty
        //
        bool empty (level level = level::core_nodes) const;

        // add
        //  - inserts address, upgrades it's level if already present 
        //
        void add (level where, const address & address);

        // ban
        //  - blocks connecting to address for established amount of days
        //  - specifying 0 days unbans the address
        //
        void ban (const address &, std::uint16_t days);

        // find
        //  - determines if we know the address and optionally on what level
        //
        bool find (const address &, level * = nullptr) const;

        // status
        //  - displays status of all connections
        //  - TODO: add function that will return data for 'overview'
        //
        void status () const;

    public:

        // operator ()
        //  - starts new connections, flushes data when needed, etc.
        //  - called whenever idle, performs actions only every 1s
        //
        void operator () ();

        // listen
        //  - adds listener based on full IP address or just port number
        //
        bool listen (const wchar_t * address);
        bool listen (std::uint16_t port);

        // discover
        //  - adds local peer discovery instance
        //
        bool discovery (const wchar_t * address);
        bool discovery (std::uint16_t port);

        // start
        //  - starts all listeners and discoverers
        //
        std::size_t start ();

        // broadcast
        //  - broadcasts the entry to all connections that are subscribed to the channel/thread/stream
        //    or everything (typically all, except leaf nodes)
        //
        std::size_t broadcast (const db::root &, const entry * data, std::size_t size);

        // broadcast
        //  - sends request to all connections (optionally with additional data)
        //
        std::size_t broadcast (enum class raddi::request::type, const void * data, std::size_t size);
        std::size_t broadcast (enum class raddi::request::type rq) {
            return this->broadcast (rq, nullptr, 0);
        }

        // keepalive
        //  - transmit keep-alive packet to eligible/idle connections
        //  - returns time delay (us) for which it's not neccessary to call this function
        //
        std::uint64_t keepalive ();

        // connect
        //  - requests coordinator to try connecting to this address, if possible
        //
        void connect (const address & a) {
            exclusive guard (this->lock);
            this->connect_requests.insert (a);
        }

        // subscribe/unsubscribe
        //  - adds/removes subscription from 'subscriptions'
        //    and also adjusts communication with peers (for leaf nodes)
        //
        void subscribe (const uuid & app, const eid & subscription);
        bool unsubscribe (const uuid & app, const eid & subscription);

        // sweep
        //  - removes and deletes all closed connections
        //
        void sweep ();

        // flush
        //  - saves changes network address indexes to disk
        //
        void flush ();

        // optimize
        //  - requests all live connections to optimize
        //
        void optimize ();

    private:
        bool is_local (const address &) const;
        void announce (const address &, bool, connection *);
        void announce_random_peers (connection *);
        void set_discovery_spread ();
        void process_download_request (const request::download *, connection *);
        
        template <typename Key>
        void report_table_history (connection *, enum class request::type, db::table <Key> *) const;
        template <enum class request::type RT, typename Key>
        bool process_table_history (const raddi::request::history * history, std::size_t size, connection *, db::table <Key> *);

        std::size_t gather_history (const eid &, request::subscription *) const;
        bool process_history (const raddi::request::subscription * history, std::size_t size, connection *);

        bool move (const address &, level, std::uint16_t = db::peerset::new_record_assessment, bool adjust = true);
        bool move (connection *, level, std::uint16_t = db::peerset::new_record_assessment);

        std::size_t select_unused_addresses (level, std::size_t n, std::map <address, level> & addresses) const;
    };
}

#endif
