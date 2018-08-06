#include "raddi_coordinator.h"
#include "raddi_connection.h"
#include "raddi_defaults.h"

#include "raddi_database_table.h"
#include "raddi_database_shard.h"

#include "../common/directory.h"

raddi::coordinator::coordinator (db & database)
    : provider ("coordinator")
    , database (database)
    , subscriptions (database.path, L"subscriptions")
    , blacklist (database.path, L"blacklist")
    , retained (database.path, L"retained") {

    if (database.settings.store_everything) {
        db::row top;
        if (database.data->top (&top)) {
            this->core_sync_threshold = top.id.timestamp - database.settings.synchronization_base_offset;
        } else {
            this->core_sync_threshold = raddi::now () - database.settings.synchronization_threshold;
        }
    }

    auto path = database.path + L"\\network";
    if (directory::create (path.c_str ())) {
        try {
            for (auto level = 0; level != levels; ++level) {
                this->database.peers [level]->load (path, AF_INET, level);
                this->database.peers [level]->load (path, AF_INET6, level);
            }
        } catch (const std::bad_alloc &) {
            raddi::log::error (component::database, 20);
        }
    } else {
        raddi::log::error (component::database, 0x21, path);
    }

    this->subscriptions.load ();
    this->blacklist.load ();
    this->retained.load ();
};

bool raddi::coordinator::find (const address & a) const {
    level unused;
    return this->find (a, unused);
}
bool raddi::coordinator::find (const address & a, level & result) const {
    for (auto l = levels - 1; l >= 0; --l) {
        if (this->database.peers [l]->count (a)) {
            result = (level) l;
            return true;
        }
    }
    return false;
}

std::uint64_t raddi::coordinator::keepalive () {
    auto period = 1000uLL * this->settings.keep_alive_period;
    auto now = raddi::microtimestamp ();
    auto next = now + period;

    immutability guard (this->lock);
    for (auto & connection : connections) {
        next = connection.keepalive (now, next, period);
    }

    return next - now;
}

void raddi::coordinator::sweep () {
    exclusive guard (this->lock);

    auto ii = this->connections.begin ();
    auto ie = this->connections.end ();
    
    if (ii != ie) {
        do {
            if (ii->retired && !ii->pending ()) {
                ii = this->connections.erase (ii);
                ie = this->connections.end ();
            } else {
                ++ii;
            }
        } while (ii != ie);

        if (ii == ie) {
            this->started = raddi::now ();
        }
    }
}

void raddi::coordinator::flush () {
    this->database.peers [blacklisted_nodes]->prune (raddi::now () / 86400);
    for (auto level = 0; level != levels; ++level) {
        this->database.peers [level]->save ();
    }

    this->subscriptions.flush ();
    this->blacklist.flush ();
    this->retained.flush ();
}

void raddi::coordinator::optimize () {
    exclusive guard (this->lock);
    for (auto & connection : this->connections) {
        if (connection.secured && !connection.retired) {
            connection.optimize ();
        }
        // TODO: close congested connections // connection.buffer_size () > threshold?
        //       don't terminate all, settings.connections? only those that did not recently asked for subscription/download?

    }
}

void raddi::coordinator::terminate () {
    this->lock.acquire_shared ();

    // cancel connections and listeners

    for (auto & listener : this->listeners) {
        listener.stop ();
    }
    for (auto & discoverer : this->discoverers) {
        discoverer.stop ();
    }
    for (auto & connection : this->connections) {
        connection.cancel ();
    }

    // wait for connections to finish
    //  - limit waiting to 15s, then forcefully terminate connections
    //  - 'lock' needs to be released at least once for a short delay before
    //    the stopped facilities above are released to allow them to process
    //    cancellation callbacks which would otherwise crash
    //     - TODO: improve by having Listener/UdpPoint some 'terminated' state
    //             and wait for all of them to enter it before clearing

    const auto threshold = raddi::now () + 15;
    do {
        this->lock.release_shared ();
        
        Sleep (100);
        this->sweep ();

        this->lock.acquire_shared ();
    } while (!this->connections.empty () && raddi::older (raddi::now (), threshold));

    this->flush ();

    if (!this->connections.empty ()) {
        this->report (log::level::stop, 0x20, this->connections.size ());
    }

    this->listeners.clear ();
    this->discoverers.clear ();
    this->connections.clear ();
    this->lock.release_shared ();
}

void raddi::coordinator::operator() () {

    // ensure at most 1Hz pacing
    //  - we can then simply use value of (settings.connection - secured) as a guideline
    //    on how many connections to try to connect in the cycle, as generally connection
    //    that won't connect within a second won't connect at all

    const auto now = raddi::now ();
    if (this->pacing != now) {
        this->pacing = now;
    } else
        return;

    std::size_t attempting [levels];
    std::size_t connected [levels];
    std::size_t secured = this->active (attempting, connected);

    this->lock.acquire_shared ();
    std::size_t total = this->connections.size ();
    this->lock.release_shared ();

    if ((total < this->settings.max_connections) || (this->settings.max_connections == 0)) {

        // want this many new connections
        std::size_t n = 0;
        if (secured < this->settings.connections) {
            n = this->settings.connections - secured;
        }

        // but keep the number of parallel attempts low
        if (n > raddi::defaults::coordinator_max_concurrent_connection_attempts) {
            n = raddi::defaults::coordinator_max_concurrent_connection_attempts;
        }

        // never overflow hard maximum
        if (this->settings.max_connections) {
            if (n > this->settings.max_connections - total) {
                n = this->settings.max_connections - total;
                this->connect_one_more_announced_node = false;
            }
        }

        // if we keep trying for more connections constantly for some time, query peers for more peers
        if (n) {
            if (raddi::older (this->last_peers_query, now - this->settings.more_peers_query_delay)) {
                this->last_peers_query = now;
                for (auto & connection : this->connections) {
                    if (connection.secured && !connection.retired) {
                        connection.send (request::type::peers);
                    }
                }
            }
        } else {
            this->last_peers_query = now;
        }

        // map simply avoids connecting to the same peer multiple times

        std::map <address, level> addresses;

        if (n) {

            // follow core node connection limits
            //  - we don't want to DDoS the main pillars

            if (attempting [core_nodes] < this->settings.max_core_connections) {
                if ((connected [core_nodes] < this->settings.min_core_connections)

                    // override minimum core connection limit if still 0 or 1 total connections after 30s

                    || ((secured <= raddi::defaults::coordinator_override_min_core_connections_count)
                        && (now - this->started) > raddi::defaults::coordinator_override_min_core_connections_delay)) {

                    n -= select_unused_addresses (core_nodes, 1, addresses);
                }
            }
        }

        // try addresses requested by user or otherwisefrom the outside

        this->lock.acquire_exclusive ();
        while ((n > 0) && !this->connect_requests.empty ()) {
            addresses.insert ({ *this->connect_requests.begin (), announced_nodes });
            this->connect_requests.erase (this->connect_requests.begin ());
            --n;
        }
        this->lock.release_exclusive ();

        // okay, try next node categories

        n -= select_unused_addresses (established_nodes, std::min (n, this->settings.connections / 2), addresses);
        n -= select_unused_addresses (validated_nodes, std::min (n, this->settings.connections / 4), addresses);

        if (n || this->connect_one_more_announced_node) {
            this->connect_one_more_announced_node = false;
            select_unused_addresses (announced_nodes, 1, addresses);
        }

        if (!addresses.empty ()) {
            exclusive guard (this->lock);

            for (const auto & [address, level] : addresses) {
                try {
                    this->connections.emplace_front (address, level).connect ();
                } catch (const raddi::log::exception &) {
                    this->ban (address, 64); // TODO: constants.h or settings?, bad address ban days (64)
                }
            }
        }
    }

    for (auto & discoverer : this->discoverers) {
        if (raddi::older (discoverer.history, now - this->settings.local_peer_discovery_period)) {
            discoverer.announce ();
        }
    }

    this->flush ();
}

std::size_t raddi::coordinator::select_unused_addresses (level lvl , std::size_t amount, std::map <address, level> & addresses) const {
    std::size_t n = 0;
    if (!this->database.peers [lvl]->empty ()) {

        for (auto i = 0u; i != amount; ++i) {
            auto a = this->database.peers [lvl]->select (this->random_distribution (this->random_generator));
            if (!this->inuse (a)
                    && !addresses.count (a)
                    && !this->database.peers [blacklisted_nodes]->count_ip (a)) {

                this->report (log::level::event, 0x21, lvl, a);
                addresses [a] = lvl;
                ++n;
            }
        }
    }
    return n;
}

bool raddi::coordinator::process (const unsigned char * data, std::size_t size, connection * connection) {
    if (request::validate (data, size)) {

        // TODO: evaluate need for locking 'connection' against getting destroyed from under our hands,
        //       add lock to prevent setting connection->retired = true while any callback is executing

        if (this->settings.max_requests_per_minute) {
            const auto now = raddi::now ();

            // increment entry for current timestamp
            //  - if it was zero, it means time moved by more than a second
            //    and there might be entries older than 1 minute to cleanup

            if (++connection->request_limiter [now] == 1u) {

                auto ii = connection->request_limiter.begin ();
                auto ie = connection->request_limiter.end ();

                while ((ii != ie) && (raddi::older (ii->first, now - 60u))) {
                    ii = connection->request_limiter.erase (ii);
                }
            }
            
            auto requests = 0u;
            for (const auto & record : connection->request_limiter) {
                requests += record.second;
            }

            // simply ignore more than moderate amount of requests per second 
            //  - report it, but only max once per second

            if (requests >= this->settings.max_requests_per_minute) {
                if (connection->request_limiter_report_time != now) {
                    connection->request_limiter_report_time = now;

                    this->report (log::level::data, 0x20, connection->peer, this->settings.max_requests_per_minute);
                }
                return true;
            }
        }

        const auto r = reinterpret_cast <const request *> (data);

        switch (r->type) {
            case request::type::ipv4peer:
            case request::type::ipv6peer:
                break; // reported below separately
            default:
                this->report (log::level::note, 8, r->type, sizeof (request), size - sizeof (request), connection->peer);
        }

        switch (r->type) {

            // initial
            //  - only validate correct protocol

            case request::type::initial:
                return std::memcmp (r->content (), raddi::protocol::magic, sizeof raddi::protocol::magic) == 0
                    || this->report (log::level::data, 0x25, connection->peer);

            // listening request
            //  - peer announces port number it supposedly listens on
            //  - save to announced_nodes list to try this address in near future

            case request::type::listening:
                if (auto port = *reinterpret_cast <const std::uint16_t *> (r->content ())) {
                    auto address = connection->peer;
                    address.port = port;

                    // only add to announced nodes if not already known

                    if (this->find (address)) {
                        this->report (log::level::note, 0x20, (unsigned int) port);
                    } else {
                        this->database.peers [announced_nodes]->insert (address);
                        this->connect_one_more_announced_node = true;
                        this->report (log::level::note, 0x21, address);
                    }
                }
                break;

            // peers
            //  - peer requests a few random peer addresses to connected to
            //  - respond and also significantly restrict frequency the peer can query peers

            case request::type::peers:
                this->announce_random_peers (connection);
                connection->request_limiter [raddi::now ()] += this->settings.max_requests_per_minute / 3;
                break;

            // ipv4peer/ipv6peer
            //  - someone else verified some other peer address

            case request::type::ipv4peer:
            case request::type::ipv6peer: {
                address a;

                switch (r->type) {
                    case request::type::ipv4peer:
                        a.family = AF_INET;
                        std::memcpy (&a.address4,
                                     reinterpret_cast <const request::ipv4peer *> (r->content ())->address,
                                     sizeof a.address4);
                        break;
                    case request::type::ipv6peer:
                        a.family = AF_INET6;
                        std::memcpy (&a.address6,
                                     reinterpret_cast <const request::ipv6peer *> (r->content ())->address,
                                     sizeof a.address6);
                        break;
                }

                // peers are not allowed to verify themselves
                //  - outbound connected connections can verify other ports on the same IP address
                //  - inbound connection may not verify other ports on the same IP address

                if (connection->peer.port) {
                    a.port = reinterpret_cast <const request::newpeer *> (r->content ())->port;
                } else {
                    a.port = 0;
                }

                this->report (log::level::note, 9, r->type, sizeof (request), size - sizeof (request), connection->peer, a);

                if (a != connection->peer) {
                    if (a.port == 0) {
                        a.port = reinterpret_cast <const request::newpeer *> (r->content ())->port;
                    }

                    // accept local network addresses only from peers on local network

                    if (a.accessible () || !connection->peer.accessible ()) {

                        // core nodes may announce other core node
                        //  - if we already know that node, upgrade it's status

                        if ((reinterpret_cast <const request::newpeer *> (r->content ())->flags & 0x0001) && (connection->level == core_nodes)) {
                            this->move (a, core_nodes);
                            this->report (log::level::note, 0x23, connection->peer, a);
                        } else {
                            this->database.peers [announced_nodes]->erase (a);
                            if (!this->find (a)) {
                                this->database.peers [validated_nodes]->insert (a);
                                this->report (log::level::note, 0x22, connection->peer, a);
                            }
                        }
                    } else {
                        this->report (log::level::data, 0x22, connection->peer, a);
                    }
                } else {
                    // don't display warning within local network
                    if (connection->peer.accessible ()) {
                        this->report (log::level::data, 0x21, connection->peer);
                    }
                }
            } break;

            case request::type::identities:
                if (this->settings.channels_synchronization_participation)
                    return this->process_table_history <request::type::identities> (reinterpret_cast <const request::history *> (r->content ()),
                                                                                    size, connection, database.identities.get ());
                else
                    return true;
            case request::type::channels:
                if (this->settings.channels_synchronization_participation)
                    return this->process_table_history <request::type::channels> (reinterpret_cast <const request::history *> (r->content ()),
                                                                                  size, connection, database.channels.get ());
                else
                    return true;

            case request::type::subscribe:
                if (auto subscription = reinterpret_cast <const request::subscription *> (r->content ())) {
                    auto subscription_size = size - sizeof (request);

                    if (subscription->history.is_valid_size (subscription_size)) {

                        // TODO: connection->history_extension = ;

                        connection->subscriptions.subscribe (subscription->channel, this->settings.max_individual_subscriptions);
                        return this->process_history (subscription, subscription_size, connection);
                    } else {
                        // TODO: report
                        // disconnect peer so he can try to find someone else who understands this request
                        return false;
                    }
                }
                break;

            case request::type::download:
                this->process_download_request (reinterpret_cast <const request::download *> (r->content ()), connection);
                break;

            case request::type::everything:
                connection->subscriptions.subscribe_to_everything ();
                break;

            case request::type::unsubscribe:
                connection->subscriptions.unsubscribe (*reinterpret_cast <const eid *> (r->content ()));
                break;
        }
        return true;
    } else
        return false;
}

template <enum class raddi::request::type RT, typename Key>
bool raddi::coordinator::process_table_history (const raddi::request::history * history, std::size_t size,
                                                raddi::connection * connection, db::table <Key> * table) {
    auto map = history->decode (size - sizeof (request));
    auto transmitter = [connection] (const auto & row, const auto & detail, std::uint8_t * data) {
        connection->send (data, (std::size_t) row.data.length + sizeof (raddi::entry));
    };

    std::uint32_t origin = 0;
    std::uint32_t oldest = 0;
    std::uint32_t latest = 0;
    std::uint32_t total = 0;

    if (!map.empty ()) {
        oldest = map.cbegin ()->first.first;
        latest = map.crbegin ()->first.second;

        for (const auto & m : map) {
            total += m.second;
        }
    }
    if (oldest > 0x70000000u) {
        origin = oldest - 0x70000000u;
    }

    this->report (log::level::event, 0x27, connection->peer, RT,
                  oldest, latest, map.size (), size - sizeof (request), total, history->threshold);

    // if there is any history
    if (!map.empty ()) {

        // start with total ancient history
        std::size_t n = table->count (origin, oldest - 1);
        if (n) {
            database.identities->select (origin, oldest - 1, transmitter);
            this->report (log::level::note, 0x27, connection->peer, RT, origin, oldest - 1, 0, n);
        }

        // compare peer's ranges against amount of data we have
        for (const auto & m : map) {
            n = table->count (m.first.first, m.first.second);

            this->report (log::level::note, 0x27, connection->peer, RT, m.first.first, m.first.second, m.second, n);

            // if we have more entries than peer does
            if (n > m.second) {
                table->select (m.first.first, m.first.second, transmitter);
            }
        }
    }

    // and finish with the most recent data
    table->select (history->threshold ? history->threshold : origin, raddi::now (), transmitter);
    return true;
}

bool raddi::coordinator::process_history (const raddi::request::subscription * subscription, std::size_t size, connection * connection) {
    auto map = subscription->history.decode (size);
    auto channel = subscription->channel;
    auto constrain = [channel] (const auto & row, const auto & detail) {
        return channel == row.top ().channel
            || channel == row.top ().thread;
    };
    auto decission = [] (const auto & row, const auto & detail) {
        return true;
    };
    auto transmitter = [connection] (const auto & row, const auto & detail, std::uint8_t * data) {
        connection->send (data, (std::size_t) row.data.length + sizeof (raddi::entry));
    };

    std::uint32_t oldest = 0;
    std::uint32_t latest = 0;
    std::uint32_t total = 0;

    if (!map.empty ()) {
        oldest = map.cbegin ()->first.first;
        latest = map.crbegin ()->first.second;

        for (const auto & m : map) {
            total += m.second;
        }
    }
    this->report (log::level::event, 0x27, connection->peer, channel,
                  oldest, latest, map.size (), size, total, subscription->history.threshold);

    for (const auto & m : map) {
        auto n = this->database.data->count (m.first.first, m.first.second);
        this->report (log::level::note, 0x27, connection->peer, channel, m.first.first, m.first.second, m.second, n);

        // if we have more entries than peer does
        if (n > m.second) {
            this->database.data->select (m.first.first, m.first.second, constrain, decission, transmitter);
        }
    }

    // and finish with the most recent data
    this->database.data->select (subscription->history.threshold, raddi::now (), constrain, decission, transmitter);
    return true;
}

void raddi::coordinator::process_download_request (const request::download * download, connection * connection) {
    auto now = raddi::now ();
    auto parent = download->parent;
    auto threshold = download->threshold;

    if (parent.isnull ()) {
        if (!this->settings.full_database_downloads_allowed) {
            this->report (log::level::data, 0x26, connection->peer);
            return;
        }

        // check against limit for full download

        if (now - threshold > this->settings.full_database_download_limit) {
            threshold = now - this->settings.full_database_download_limit;
        }
        
    } else {

        // if it's nested entry, provide whole thread

        db::row row;
        if (this->database.data->get (download->parent, &row)) {
            parent = row.top ().thread;
        }

        // descending entries cannot be older than their parent

        if (raddi::older (threshold, parent.timestamp)) {
            threshold = parent.timestamp;
        }
    }

    auto decission = [] (const auto & row, const auto & detail) {
        return true;
    };
    auto transmitter = [connection] (const auto & row, const auto & detail, std::uint8_t * data) {
        connection->send (data, (std::size_t) row.data.length + sizeof (raddi::entry));
    };

    if (parent.isnull ()) {
        auto unconstrained = [] (const auto & row, const auto & detail) {
            return true;
        };
        this->report (log::level::note, 0x29, connection->peer, threshold, now);
        this->database.data->select (threshold, now, unconstrained, decission, transmitter);

    } else {
        auto constrain = [parent] (const auto & row, const auto & detail) {
            return parent == row.top ().channel
                || parent == row.top ().thread;
        };
        this->report (log::level::note, 0x28, connection->peer, threshold, now, parent);
        this->database.data->select (threshold,  now, constrain, decission, transmitter);
    }
}

void raddi::coordinator::move (const address & address, level new_level, std::uint16_t assessment) {
    level level;
    while (this->find (address, level)) {
        this->database.peers [level]->erase (address);
    }
    this->database.peers [new_level]->insert (address, assessment);
}

void raddi::coordinator::ban (const address & address, std::uint16_t days) {
    if (days) {
        this->move (address, blacklisted_nodes, raddi::now () / 86400 + days);
        this->report (log::level::event, 0x22, address, days);
    } else {
        this->database.peers [blacklisted_nodes]->erase (address);
        this->report (log::level::event, 0x28, address);
    }
}

bool raddi::coordinator::incomming (Socket && prepared, const sockaddr * remote) {

    if (this->database.peers [blacklisted_nodes]->count_ip (remote)) {
        this->report (log::level::event, 0x20, remote);
        return false;
    }

    // add to incomming list

    try {
        exclusive guard (this->lock);
        return this->connections.emplace_front (std::move (prepared), remote, blacklisted_nodes).accepted ();
    } catch (const std::bad_alloc &) {
        return false;
    }
}

bool raddi::coordinator::inuse (const address & a) const {
    address a0 = a;
    a0.port = 0;
    
    immutability guard (this->lock);

    for (const auto & connection : connections) {
        if ((connection.peer == a) || (connection.peer == a0))
            return true;
    }
    return false;
}

bool raddi::coordinator::blacklisted (const address & a) const {
    return this->database.peers [blacklisted_nodes]->count_ip (a);
}

bool raddi::coordinator::empty (level level) const {
    return this->database.peers [level]->empty ();
}

void raddi::coordinator::add (level where, const address & address) {
    level previous;
    if (this->find (address, previous)) {
        if (previous > where) {
            this->move (address, where);
        }
    } else {
        this->database.peers [where]->insert (address);
    }
}

bool raddi::coordinator::reflecting (const raddi::protocol::keyset * peer) {
    immutability guard (this->lock);

    for (const auto & connection : connections) {
        if (connection.reflecting (peer)) {
            this->ban (connection.peer, 28); // TODO: constants.h, reflected connection ban days (28)
            return true;
        }
    }
    return false;
}
bool raddi::coordinator::reciprocal (const connection * peer) {
    address other = peer->peer;
    other.port = 0;

    immutability guard (this->lock);

    for (const auto & connection : connections) {
        if (connection.secured) {
            if (&connection != peer) {
                address addr = connection.peer;
                addr.port = 0;

                if (addr == other) {
                    this->report (log::level::event, 0x25, connection.peer, 28);;
                    return true;
                }
            }
        }
    }
    return false;
}
std::size_t raddi::coordinator::active (std::size_t attempting [levels],
                                        std::size_t connected [levels]) const {
    std::size_t n = 0;
    if (attempting) {
        std::memset (attempting, 0, levels * sizeof (std::size_t));
    }
    if (connected) {
        std::memset (connected, 0, levels * sizeof (std::size_t));
    }

    immutability guard (this->lock);

    for (const auto & connection : connections) {
        if (connection.connecting) {
            if (attempting) {
                ++attempting [connection.level];
            }
        }
        if (connection.secured) {
            ++n;
            if (connected) {
                ++connected [connection.level];
            }
        }
    }
    return n;
}

bool raddi::coordinator::broadcasting () const {
    std::size_t connected [levels];
    return this->active (nullptr, connected) >= this->settings.min_connections
        && connected [core_nodes] >= this->settings.min_core_connections;
}

void raddi::coordinator::status () const {
    immutability guard (this->lock);
    for (const auto & connection : connections) {
        if (connection.secured)
            connection.status ();
    }
}

std::size_t raddi::coordinator::broadcast (const db::root & top, const entry * data, std::size_t size) {
    const bool announcement = data->is_announcement ();
    std::size_t n = 0;

    immutability guard (this->lock);
    for (auto & connection : connections) {
        if (connection.secured && !connection.retired) {

            // TODO: send to someone immediately and to others with slight delay (up to 1s?) to mess with origin analysis - Aetheral Research
            //        - this will come in hand with the queuing feature described in .send function comments

            if (announcement || connection.subscriptions.is_subscribed ({ top.channel, top.thread, data->parent, data->id })) {
                n += connection.send (data, size);
            }
        }
    }
    return n;
}

void raddi::coordinator::established (connection * connection) {

    // exchange protocol strings to verify encryption works correctly
    connection->send (request::type::initial, raddi::protocol::magic, sizeof raddi::protocol::magic);

    if (connection->peer.port) {
        // update level for successful outbound connection
        switch (connection->level) {

            case established_nodes:
                this->database.peers [connection->level]->adjust (connection->peer, +1);
                break;

            case validated_nodes:
                if (this->database.peers [connection->level]->adjust (connection->peer, +1) >= 0xFF) {
                    connection->level = established_nodes;
                    this->move (connection->peer, established_nodes);
                }
                break;

            case announced_nodes:
                connection->level = validated_nodes;
                this->move (connection->peer, validated_nodes);
                this->announce (connection->peer, false, connection);
                break;
        }

        // if not through proxy, notify peer that we are also listening (if we are listening)
        if (!socks5proxy.port) {
            for (const auto port : this->listening_ports [connection->peer.family]) {
                connection->send (request::type::listening, &port, sizeof port);
            }
        }
    } else {
        // inbound connection, give our friend a few randomly selected peer addresses
        this->announce_random_peers (connection);
    }

    // request identities and channels we may have missed

    this->report_table_history (connection, request::type::identities, this->database.identities.get ());
    this->report_table_history (connection, request::type::channels, this->database.channels.get ());

    // subscribe to content
    //  - leaf nodes enumerate subscribed channels/threads to the peer to conserve bandwidth
    //     - we request data on identity channels only from core nodes (hopefully trustworthy)
    //       to limit potential of discovering real-world identity between peers
    //  - non-leaf nodes participate in network propagation so request everything

    if (this->settings.network_propagation_participation) {
        connection->send (request::type::everything);
    } else {
        this->subscriptions.enumerate ([this, connection] (auto eid) {
            if ((eid.timestamp != eid.identity.timestamp) || (connection->level == core_nodes)) {
                
                request::subscription packet;
                if (auto size = this->gather_history (eid, &packet)) {
                    connection->send (request::type::subscribe, &packet, size);
                }
            }
        });
    }

    // core nodes sync
    //  - if connected to core node, and we allow full download queries, send one

    if ((connection->level == core_nodes) && this->settings.full_database_downloads_allowed) {
        if (auto ncs = this->core_sync_count) {

            request::download download;
            download.parent.timestamp = 0;
            download.parent.identity.timestamp = 0;
            download.parent.identity.nonce = 0;
            download.threshold = this->core_sync_threshold;
            
            if (connection->send (request::type::download, &download, sizeof download)) {
                this->core_sync_count = ncs - 1;
            }
        }
    }
}

void raddi::coordinator::subscribe (const uuid & app, const eid & subscription) {
    bool already = this->settings.network_propagation_participation
                || this->subscriptions.is_subscribed ({ subscription });

    this->subscriptions.subscribe (app, subscription);
    if (!already) {

        request::subscription packet;
        if (auto size = this->gather_history (subscription, &packet)) {

            immutability guard (this->lock);
            for (auto & connection : this->connections) {
                if (connection.secured && !connection.retired) {
                    connection.send (request::type::subscribe, &packet, size);
                }
            }
        }
    }
}

bool raddi::coordinator::unsubscribe (const uuid & app, const eid & subscription) {
    if (this->subscriptions.unsubscribe (app, subscription)) {

        // if we are not subscribing to everything from peers
        // and after removal we no longer want that subscription
        // notify peers about it

        if (!this->settings.network_propagation_participation) {
            if (!this->subscriptions.is_subscribed ({ subscription })) {

                immutability guard (this->lock);
                for (auto & connection : this->connections) {
                    if (connection.secured && !connection.retired) {
                        connection.send (request::type::unsubscribe, &subscription, sizeof subscription);
                    }
                }
            }
        }
        return true;
    } else
        return false;
}

template <typename Key>
void raddi::coordinator::report_table_history (raddi::connection * connection, enum class raddi::request::type rq, db::table <Key> * table) const {
    struct state {
        std::uint32_t now = raddi::now ();
        std::uint32_t threshold = 0;
        std::uint32_t thresholds [request::history::depth];
        std::uint16_t flags = 0x0000;
        std::size_t numbers [request::history::depth];
        std::size_t length = 0;
        std::size_t tooold = 0;
        std::size_t sources = 0;
        std::size_t total = 0;
        bool first_iteration = true;
    } s;

    // accumulate base timestamps and sizes of shards into short list (max length is 'depth')

    table->enumerate_shard_info ([this, &s] (std::uint32_t t, std::size_t n) {
        if (!raddi::older (t, s.now - this->database.settings.synchronization_base_offset)) {
            return false;
        }
        if (s.first_iteration) {
            s.first_iteration = false;
            s.threshold = t;
            s.numbers [0] = -1;
            s.thresholds [0] = t;
        }

        s.sources += 1;
        s.total += n;

        // TODO: tune '4' scaling factor according to real-world workload

        if (raddi::older (t, s.threshold + (s.now - s.threshold) / 4 + 1)) {
            s.numbers [s.length] += n;
            // s.flags |= 0x0001; // can be further elaborated, TODO: enable after support implemented
        } else {
            s.threshold = t;
            if (++s.length < request::history::depth) {
                s.numbers [s.length] = n - 1;
                s.thresholds [s.length] = t;
            } else
                return false;
        }
        return true;
    });

    request::history history;
    std::memset (&history, 0, sizeof history);

    if (s.length == 0) {
        history.threshold = 0; // everything, this is special-cased for identities and channels
    } else {
        history.threshold = s.threshold;
    }

    history.flags = s.flags;

    auto i = s.length;
    auto tx = s.threshold;
    while (i--) {
        auto t = tx - s.thresholds [i] - 1;
        history.span [i].threshold [0] = (t >> 0) & 0xFF;
        history.span [i].threshold [1] = (t >> 8) & 0xFF;
        history.span [i].threshold [2] = (t >> 16) & 0xFF;

        auto n = s.numbers [i];
        if (n > 0x00FFFFFF) {
            n = 0x00FFFFFF;
        }
        history.span [i].number [0] = (n >> 0) & 0xFF;
        history.span [i].number [1] = (n >> 8) & 0xFF;
        history.span [i].number [2] = (n >> 16) & 0xFF;

        tx = s.thresholds [i];
    }

    connection->send (rq, &history, history.size (s.length));

    this->report (log::level::event, 0x26,
                  connection->peer, rq,
                  s.length ? s.thresholds [0] : 0, history.threshold,
                  s.length, s.total, s.sources, history.size (s.length));
}

std::size_t raddi::coordinator::gather_history (const eid & channel, raddi::request::subscription * result) const {
    struct state {
        bool first_iteration = true;
        std::uint32_t now = raddi::now ();
        std::uint32_t threshold = 0;
        std::uint32_t thresholds [request::subscription::depth];
        std::size_t numbers [request::subscription::depth];
        std::size_t length = 0;
    } s;

    std::size_t total = 0;

    // accumulate number of entries into short list (max length is 'depth')

    try {
        total = this->database.data->select (
            s.now - this->database.settings.synchronization_threshold,
            s.now - this->database.settings.synchronization_base_offset,

            [&channel] (const auto & row, const auto &) {
                return channel == row.top ().channel
                    || channel == row.top ().thread;
            },
            [this, &s, &total] (const auto & row, const auto & detail) {
                const auto t = row.id.timestamp;

                if (s.first_iteration) {
                    s.first_iteration = false;
                    s.threshold = t;
                    s.numbers [0] = -1;
                    s.thresholds [0] = t;
                }

                // TODO: tune '3' scaling factor according to real-world workload

                if (raddi::older (t, s.threshold + (s.now - s.threshold) / 3 + 1)) {
                    ++s.numbers [s.length];
                } else {
                    s.threshold = t;
                    if (++s.length < request::history::depth) {
                        s.numbers [s.length] = 0;
                        s.thresholds [s.length] = t;
                    } else {
                        total = detail.match;
                        throw true; // history depth is full, done
                    }
                }
                // don't want data
                return false;
            },
            [] (const auto &, const auto & detail, std::uint8_t *) {}
        );
    } catch (bool) {
        // finished with completely filling history depth
    }

    std::memset (result, 0, sizeof *result);
    result->channel = channel;

    if (s.length == 0) {
        result->history.threshold = s.now - this->database.settings.synchronization_threshold;
    } else {
        result->history.threshold = s.threshold;
    }

    /* // TODO: enable this when supported
    for (auto i = 0u; i != s.length; ++i) {
        if (s.numbers [i] != 0) {
            result->history.flags = 0x0001; // could be further elaborated
            break;
        }
    }// */

    auto i = s.length;
    auto tx = s.threshold;
    while (i--) {
        auto t = tx - s.thresholds [i] - 1;
        result->history.span [i].threshold [0] = (t >> 0) & 0xFF;
        result->history.span [i].threshold [1] = (t >> 8) & 0xFF;
        result->history.span [i].threshold [2] = (t >> 16) & 0xFF;

        auto n = s.numbers [i];
        if (n > 0x00FFFFFF) {
            n = 0x00FFFFFF;
        }
        result->history.span [i].number [0] = (n >> 0) & 0xFF;
        result->history.span [i].number [1] = (n >> 8) & 0xFF;
        result->history.span [i].number [2] = (n >> 16) & 0xFF;

        tx = s.thresholds [i];
    }

    auto size = result->history.size (s.length);
    this->report (log::level::event, 0x29,
                  channel,
                  s.length ? s.thresholds [0] : 0, result->history.threshold,
                  s.length, total, size); // */

    return size;
}

void raddi::coordinator::announce (const address & a, bool only, connection * cx) {
    union {
        request::newpeer common;
        request::ipv4peer v4;
        request::ipv6peer v6;
    } r;

    r.common.flags = 0;
    r.common.port = a.port;

    enum class request::type rt;
    std::size_t rcb;

    switch (a.family) {
        case AF_INET:
            rt = request::type::ipv4peer;
            rcb = sizeof (request::ipv4peer);
            std::memcpy (r.v4.address, &a.address4, sizeof a.address4);
            break;
        case AF_INET6:
            rt = request::type::ipv6peer;
            rcb = sizeof (request::ipv6peer);
            std::memcpy (r.v6.address, &a.address6, sizeof a.address6);
            break;
    }

    if (only) {
        cx->send (rt, &r, rcb);
    } else {
        immutability guard (this->lock);
        for (auto & c : this->connections) {
            if (c.secured && !c.retired && (&c != cx))
                c.send (rt, &r, rcb);
        }
    }
}

void raddi::coordinator::announce_random_peers (connection * c) {
    std::set <address> sample;

    std::size_t i = 0;
    for (int level = core_nodes; level != blacklisted_nodes; ++level) {
        if (!this->database.peers [level]->empty ()) {
            while (i < ((level + 1) * this->settings.announcement_sample_size / blacklisted_nodes)) {

                std::uint16_t assessment;
                address addr = this->database.peers [level]->select (this->random_distribution (this->random_generator), &assessment);

                // distribute local addresses only to other peers on local network
                //  - TODO: BUG, somehow we still announce local addressed to internet peers, NOTE this bug may have been fixed, test it

                if (addr.accessible () || !c->peer.accessible ()) {

                    // only insert addresses that are fresh or already validated
                    //  - this prevents endless re-sharing addresses of long dead peers

                    if (assessment >= db::peerset::new_record_assessment) {
                        sample.insert (addr);
                    }
                }
                ++i;
            }
        }
    }

    for (const auto & a : sample)
        this->announce (a, true, c);
}

void raddi::coordinator::unavailable (const connection * connection) {

    // reduce rating and eventually remove dead peers
    //  - only if we have at least one active connection
    //    as the unavailability issue may be local and temporary

    if (this->active ()) {
        if (connection->level != blacklisted_nodes) {
            if (this->database.peers [connection->level]->adjust (connection->peer, -1) == 0) {
                this->database.peers [connection->level]->erase (connection->peer);
                this->report (log::level::event, 0x23, connection->peer);
            }
        }
    }
}

void raddi::coordinator::disagreed (const connection * connection) {
    if (connection->level != blacklisted_nodes
            && this->database.peers [connection->level]->adjust (connection->peer, -0xF) == 0) {

        std::uint32_t days;
        if (connection->peer.port) {
            // outbound connecton, known node in network compromised, TODO: raddi_defaults.h
            days = 14;
        } else {
            // inbound connection, might be new version or fork, ban whole IP lightly (a day)
            days = 1;
        }
        this->ban (connection->peer, days);
    }
}

std::size_t raddi::coordinator::start () {
    std::size_t n = 0;
    for (auto & listener : this->listeners) {
        n += listener.start ();
    }
    for (auto & discoverer : this->discoverers) {
        n += discoverer.start ();
    }
    this->report (log::level::event, 0x24, n);
    return n;
}

bool raddi::coordinator::listen (std::uint16_t port) {
    bool any = false;
    if (port > 0 && port < 65536) {

        // failures to construct Listener are reported when thrown

        exclusive guard (this->lock);
        try {
            this->listeners.emplace_back (AF_INET, (std::uint16_t) port);
            this->listening_ports [AF_INET].insert ((std::uint16_t) port);
            any = true;
        } catch (const std::exception &) {}
        try {
            this->listeners.emplace_back (AF_INET6, (std::uint16_t) port);
            this->listening_ports [AF_INET6].insert ((std::uint16_t) port);
            any = true;
        } catch (const std::exception &) {}
    }
    return any;
}

bool raddi::coordinator::listen (const wchar_t * address) {
    if (std::wcschr (address, L'.') || std::wcschr (address, L':')) {

        SOCKADDR_INET a;
        if (StringToAddress (a, address)) {
            switch (a.si_family) {
                case AF_INET:
                    if (a.Ipv4.sin_port == 0) {
                        a.Ipv4.sin_port = htons (raddi::defaults::coordinator_listening_port);
                    }
                    break;
                case AF_INET6:
                    if (a.Ipv6.sin6_port == 0) {
                        a.Ipv6.sin6_port = htons (raddi::defaults::coordinator_listening_port);
                    }
                    break;
            }

            exclusive guard (this->lock);
            try {
                this->listeners.emplace_back (a);
                this->listening_ports [a.si_family].insert (ntohs (a.Ipv4.sin_port));
                return true;
            } catch (const std::exception &) {
                // failure to construct Listener, already reported
            }
        }
        return false;
    } else
        return this->listen ((std::uint16_t) std::wcstoul (address, nullptr, 10));
}

bool raddi::coordinator::discovery (std::uint16_t port) {
    bool any = false;
    if (port > 0 && port < 65536) {

        // failures to construct discovery are reported when thrown

        exclusive guard (this->lock);
        if (!this->listening_ports [AF_INET].empty ()) {
            try {
                this->discoverers.emplace_back (AF_INET, (std::uint16_t) port).announcement = *this->listening_ports [AF_INET].begin ();
                any = true;
            } catch (const std::exception &) {}
        }
        if (!this->listening_ports [AF_INET6].empty ()) {
            try {
                this->discoverers.emplace_back (AF_INET6, (std::uint16_t) port).announcement = *this->listening_ports [AF_INET6].begin ();
                any = true;
            } catch (const std::exception &) {}
        }
    }
    if (any) {
        this->set_discovery_spread ();
    }
    return any;
}

bool raddi::coordinator::discovery (const wchar_t * address) {
    if (std::wcschr (address, L'.') || std::wcschr (address, L':')) {

        SOCKADDR_INET a;
        if (StringToAddress (a, address)) {
            switch (a.si_family) {
                case AF_INET:
                    if (a.Ipv4.sin_port == 0) {
                        a.Ipv4.sin_port = htons (raddi::defaults::coordinator_discovery_port);
                    }
                    break;
                case AF_INET6:
                    if (a.Ipv6.sin6_port == 0) {
                        a.Ipv6.sin6_port = htons (raddi::defaults::coordinator_discovery_port);
                    }
                    break;
            }

            if (!this->listening_ports [a.si_family].empty ()) {
                exclusive guard (this->lock);
                try {
                    this->discoverers.emplace_back (a).announcement = *this->listening_ports [a.si_family].begin ();
                    this->set_discovery_spread ();
                    return true;
                } catch (const std::exception &) {
                    // failure to construct discovery, already reported
                }
            }
        }
        return false;
    } else
        return this->discovery ((std::uint16_t) std::wcstoul (address, nullptr, 10));
}

void raddi::coordinator::set_discovery_spread () {
    const auto base = raddi::now () - this->settings.local_peer_discovery_period + 3;
    const auto n = this->discoverers.size ();
    std::size_t i = 0;

    for (auto & discoverer : this->discoverers) {
        discoverer.history = base + std::uint32_t (i * this->settings.local_peer_discovery_period / n);
        ++i;
    }
}