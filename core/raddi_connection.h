#ifndef RADDI_CONNECTION_H
#define RADDI_CONNECTION_H

#include "raddi_request.h"
#include "raddi_protocol.h"
#include "raddi_timestamp.h"
#include "raddi_coordinator.h"
#include "raddi_subscriptions.h"

#include "../node/server.h"
#include "../common/log.h"

namespace raddi {

    // socks5proxy
    //  - address of optimistic SOCKS5 proxy (i.e. Tor)
    //  - if .port is 0, then socks5t proxy is NOT used
    //
    extern address socks5proxy;

    // connection
    //  - represents RADDI.net peer connection state
    //
    class connection
        : public Connection
        , virtual raddi::log::provider <raddi::component::server> {

        virtual bool inbound (unsigned char * data, std::size_t & size) override;
        virtual bool connected () override;
        virtual void overloaded () override;
        virtual void disconnected () override;

        void discord ();
        void out_of_memory ();
        bool head (raddi::protocol::initial * peer);
        bool decode (const unsigned char * data, std::size_t size);
        bool message (const unsigned char * entry, std::size_t size);

        union {
            protocol::proposal * proposal; // this->state < secured
            protocol::encryption * encryption; // this->state == secured
        };

    public:
        explicit connection (Socket &&, const sockaddr * peer, raddi::level level);
        explicit connection (const address & peer, raddi::level level);
        ~connection ();

        std::map <std::uint32_t, std::uint32_t> request_limiter; // TODO: separate class
        std::uint32_t                           request_limiter_report_time = 0;
        std::uint32_t                           unsolicited = 0;
        // std::map <raddi::eid, std::uint32_t>    history_extension;
        std::uint32_t                           rejected = 0;

        raddi::address  peer; // inbound connections have port set to 0
        raddi::level    level; // not strictly required here, coordinator could do search

        enum class state : std::uint8_t {
            pending,
            secured,
            retired
        } state = state::pending;

        subscriptions   subscriptions;

        using Connection::connecting;
        using Connection::pending;
        using Connection::optimize;
        using Connection::buffer_size;

    private:
        // queues:
        //  0 - new identities
        //  1 - new channels
        //  2 - responses to requests
        //  3 - new data queued last, ordered by timestamp, AND size
            
        // queues [4]
        // std::vector <std::uint8_t>

    public:
        std::uint64_t latest = raddi::microtimestamp ();
        std::uint64_t probed = 0;

        // age
        //  - 
        //
        std::uint64_t age (std::uint64_t now = raddi::microtimestamp ()) const {
            return raddi::microtimestamp () - std::max (this->probed, this->latest);
        }

        struct counter messages;
        struct counter keepalives;

        bool is_inbound () const { return this->peer.port == 0; }
        bool is_outbound () const { return this->peer.port != 0; }

        // connect
        //  - starts processing incomming traffic on the connection
        //    and asynchronously begins establishing connection to peer
        //
        bool connect () {
            if (this->Connection::connect (socks5proxy.port ? socks5proxy : this->peer)) {
                return true;
            } else {
                this->disconnected ();
                return false;
            }
        }

        // accepted
        //  - starts processing incomming traffic on the connection
        //    that was accepted by Listener (already connected)
        //
        bool accepted () {
            if (this->Receiver::accepted ()) {
                return true;
            } else {
                this->disconnected ();
                return false;
            }
        }

        // send
        //  - using Connection Transmitter facilities encodes provided data
        //    directly into either transmission buffer and initiates transmission
        //    or into buffer of pending data
        //
        bool send (const void * data, std::size_t size);

        // send
        //  - assembles full raddi::request packet and sends it just like 'send' above
        //
        bool send (enum class raddi::request::type, const void * payload, std::size_t size);
        bool send (enum class raddi::request::type t) {
            return this->send (t, nullptr, 0);
        }

        // keepalive
        //  - transmits keep-alive token if there's no other transmission pending or queued
        //    and updates expected time of a next keep-alive
        //  - parameters: micronow - raddi::microtimestamp retrieved earlier
        //                expected - current microsecond delay until next keep-alive
        //                period - microsecond keep-alive period
        //  - returns new expected microsecond delay until next keepalive needs to be transmitted
        //
        std::uint64_t keepalive (std::uint64_t micronow, std::uint64_t expected, std::uint64_t period);

        // cancel
        //  - closes the socket interrupting pending transmissions and receives
        //    and schedules the connection for destruction
        //
        void cancel () {
            this->Connection::terminate ();
        }

        // reflecting
        //  - checks whether 'head' matches this connection (not established yet)
        //    to determine if we aren't accidentaly connecting back to ourselves
        //
        bool reflecting (const raddi::protocol::keyset * peer) const {
            return this->state == state::pending
                && !std::memcmp (peer->inbound_nonce, this->proposal->inbound_nonce, sizeof peer->inbound_nonce)
                && !std::memcmp (peer->outbound_nonce, this->proposal->outbound_nonce, sizeof peer->outbound_nonce);
        }

        // status
        //  - displays/logs a short connection statistics log note
        //
        void status () const;

        // status_report
        //  - 
        //
        std::wstring status_report () const;
    };
}

#endif
