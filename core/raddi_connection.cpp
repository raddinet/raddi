#include "raddi_connection.h"
#include "raddi_entry.h"
#include <algorithm>
#include <set>

#pragma warning (disable:6262) // function stack size warning
#pragma warning (disable:26819) // unannotated fallthrough

raddi::address raddi::socks5proxy;

namespace {
    template <typename T>
    static std::wstring make_connection_instance_name (SOCKET s, wchar_t io, T address) {
        return std::to_wstring (s) + io + raddi::log::translate (address, std::wstring ());
    }
    
    raddi::address marked_inbound (raddi::address a) {
        a.port = 0;
        return a;
    }
}

raddi::connection::connection (Socket && s, const sockaddr * addr, raddi::level level)
    : Socket (std::move (s))
    , Connection (std::move (s))
    , provider ("connection", make_connection_instance_name (*this, L'\x2193', addr))
    , proposal (new protocol::proposal)
    , peer (marked_inbound (addr))
    , level (level) {}

raddi::connection::connection (const address & addr, raddi::level level)
    : Socket (addr.family, SOCK_STREAM, IPPROTO_TCP)
    , Connection (addr.family)
    , provider ("connection", make_connection_instance_name (*this, L'\x2191', addr))
    , proposal (new protocol::proposal)
    , peer (addr)
    , level (level) {}

raddi::connection::~connection () {
    if (this->secured) {
        delete this->encryption;
        this->encryption = nullptr;
    } else {
        delete this->proposal;
        this->proposal = nullptr;
    }
}

bool raddi::connection::connected () {
    std::size_t prologue;
    if (socks5proxy.port && this->peer.accessible ()) { // accessible is false for inbound connections
        prologue = 7 + this->peer.size ();
    } else {
        prologue = 0;
    }
    if (auto buffer = this->prepare (sizeof (raddi::protocol::initial) + prologue)) {
        if (prologue) {
            buffer [0] = 0x05; // SOCKS5
            buffer [1] = 0x01; //  - only 1 method of auth supported
            buffer [2] = 0x00; //  - support NO AUTHENTICATION method
            buffer [3] = 0x05; // SOCKS5
            buffer [4] = 0x01; //  - CONNECT command
            buffer [5] = 0x00; //  - RESERVED

            switch (this->peer.family) {
                case AF_INET:
                    buffer [6] = 0x01; // IPv4
                    std::memcpy (&buffer [7], &this->peer.address4, sizeof this->peer.address4);
                    break;
                case AF_INET6:
                    buffer [6] = 0x04; // IPv6
                    std::memcpy (&buffer [7], &this->peer.address6, sizeof this->peer.address6);
                    break;
                default:
                    return false;
            }
            buffer [prologue - 2] = this->peer.port / 256;
            buffer [prologue - 1] = this->peer.port % 256;
        }
        this->proposal->propose (reinterpret_cast <raddi::protocol::initial *> (buffer + prologue), this->peer.port != 0); // this->peer.port != 0 means outbound connection;
        return this->transmit (buffer, sizeof (raddi::protocol::initial) + prologue);
    } else
        return false;
}

void raddi::connection::overloaded () {
    if (!socks5proxy.port)
        if (auto buffer = this->prepare (sizeof (std::uint32_t))) {
            std::memset (buffer, 0, sizeof (std::uint32_t));
            this->transmit (buffer, sizeof (std::uint32_t));
        }
}

bool raddi::connection::send (const void * data, std::size_t size) {
    if (size > raddi::protocol::max_payload)
        return false;

    // TODO: add multiple queues and prioritize smaller entries before larger ones,
    //       but sort by size only entries with the same timestamp (send older first)
    //     - queue priorities:
    //          1) new identities transmitted first
    //          2) new channels transmitted second
    //          3) responses to requests next
    //          4) new data queued last, ordered by size?
    //     - queues will need to feature millisecond delay mark

    // transmitter lock is required except in 'connected' and 'overloaded'

    exclusive guard (this->Transmitter::lock);
    if (auto message = this->prepare (size + raddi::protocol::frame_overhead)) {
        auto length = this->encryption->encode (message, size + raddi::protocol::frame_overhead,
                                                static_cast <const unsigned char *> (data), size);
        return this->transmit (message, length);
    } else
        return false;
}

bool raddi::connection::send (enum class raddi::request::type type, const void * data, std::size_t size) {
    if (size > raddi::request::max_payload)
        return false;

    struct {
        raddi::request header;
        std::uint8_t   payload [raddi::request::max_payload];
    } r;
    
    r.header.mark = raddi::now ();
    r.header.type = type;
    if (data && size) {
        std::memcpy (r.payload, data, size);
    }

    this->report (log::level::note, 7, type, sizeof (request), size);
    return this->send (&r, sizeof (request) + size);
}

std::uint64_t raddi::connection::keepalive (std::uint64_t now, std::uint64_t expected, std::uint64_t period) {
    if (!this->retired) {
        if (std::int64_t (now - this->latest) > std::int64_t (std::max (4 * period, 1'000'000uLL))) {
            this->report (raddi::log::level::event, 8);
            this->cancel ();
        } else
        if (this->secured) {
            if (std::int64_t (now - std::max (this->latest, this->probed)) > std::int64_t (period)) {
                if (!this->unsynchronized_is_live ()) {
                    if (auto message = this->prepare (2)) {
                        message [0] = 0x00;
                        message [1] = 0x00;
                        if (this->transmit (message, 2)) {
                            this->probed = now;
                        }
                    }
                }
            } else {
                this->probed = std::max (this->latest, this->probed);
                return std::min (expected, this->probed + period);
            }
        }
    }
    return expected;
}

void raddi::connection::status () const {
    this->report (raddi::log::level::note, 1,
                  this->retired ? "retired" : this->secured ? this->encryption->reveal () : "connecting",
                  this->buffer_size (),
                  (raddi::microtimestamp () - std::max (this->probed, this->latest)) / 1'000'000uLL,
                  this->counter, this->messages, this->keepalives,
                  this->counters.sent, this->counters.delayed);// */
}

bool raddi::connection::decode (const unsigned char * data, std::size_t size) {
    unsigned char entry [raddi::protocol::max_payload] alignas (raddi::entry);
    if (auto length = this->encryption->decode (entry, sizeof entry, data, size)) {
        try {
            if (this->message (entry, length)) {
                this->messages += length;
                this->latest = raddi::microtimestamp ();
                return true;

            } else {
                this->discord ();
            }
        } catch (const std::bad_alloc &) {
            this->out_of_memory ();
        }
    } else {
        // note: 'n' in range 1..raddi::protocol::frame_overhead-1 end up here
        this->discord ();
    }
    return false;
}

bool raddi::connection::inbound (unsigned char * data, std::size_t & n) {
    if (this->secured) {
        if (n >= 2) {
            std::uint32_t size = data [0] | (data [1] << 8);
            switch (size) {

                case 0x0000:
                    n = 2;
                    this->keepalives += n;
                    if (auto message = this->prepare (2)) {
                        message [0] = 0xFF;
                        message [1] = 0xFF;
                        return this->transmit (message, 2);
                    } else
                        return false;

                case 0xFFFF:
                    n = 2;
                    this->latest = raddi::microtimestamp ();
                    break;

                default:
                    size += sizeof (std::uint16_t);
                    if (n >= size) {
                        if (!this->decode (data, size))
                            return false;
                    }
                    n = size;
                    break;
            }
        } else {
            n = 2;
        }
    } else {
        std::size_t prologue = 0;
        if (socks5proxy.port && this->peer.accessible ()) {
            prologue = 6 + this->peer.size ();

            bool valid = true;
            switch (n) {
                default:
                case 5: if (data [4] != 0x00) valid = false; // RESERVED
                case 4: if (data [3] != 0x00) valid = false; // SUCCESS
                case 3: if (data [2] != 0x05) valid = false; // SOCKS5
                case 2: if (data [1] != 0x00) valid = false; // NO AUTHENTICATION is fine
                case 1: if (data [0] != 0x05) valid = false; // SOCKS5
            }

            if (!valid) {
                if ((n > 3) && (data [0] == 0x05 && data [1] == 0x00 && data [2] == 0x05)) {
                    if (data [3] != 6) {
                        this->report (log::level::error, 16, (unsigned int) data [3],
                                      log::rsrc_string (0x00020 + ((data [3] < 9) ? data [3] : 9)));
                    }
                } else {
                    this->report (log::level::error, 15);
                }
                return false;
            }
            if (n == 2)
                this->report (log::level::note, 4, socks5proxy);
        }

        if (n >= sizeof (raddi::protocol::initial) + prologue) {
            if (this->head (reinterpret_cast <raddi::protocol::initial *> (data + prologue))) {
                this->secured = true;
                n = sizeof (raddi::protocol::initial) + prologue;
            } else {
                this->discord ();
                return false;
            }
        } else
            if (n >= sizeof (std::uint32_t) && (*reinterpret_cast <const std::uint32_t *> (data) == 0)) {
                // peer is overloaded, disconnect and try later
                return false;
            } else {
                n = sizeof (raddi::protocol::initial) + prologue;
            }
    }
    return true;
}
