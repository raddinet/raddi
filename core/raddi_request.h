#ifndef RADDI_REQUEST_H
#define RADDI_REQUEST_H

#include "raddi_entry.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <string>
#include <map>

namespace raddi {

    // request
    //  - network coordination request, transmitted between nodes, generated by coordinator
    //  - payload follows; accessible by .content()
    // 
    struct request __attribute__((ms_struct)) {

        // mark
        //  - lower 24 bits of timestamp
        //
        std::uint32_t   mark : 24;
        
        // type
        //  - highest 8 bits
        //  - request type, single byte, content structures defined below
        //  - type is 'std::uint32_t' for this to work correctly as a bitfield below
        //
        enum class type : std::uint32_t {

            // initial -> raddi::protocol::magic
            //  - first packet exchanged to quickly verify encryption works
            //  - TODO: append IP/port how I see the peer connection?
            //
            initial = 0x00,

            // security_check
            //  - TBD: not implemented yet; should verify there is no man in the middle on the protocol
            //
            security_check = 0x01,

            // listening -> std::uint16_t
            //  - announcement of port number the peer listens on, 2 byte number follows, little endian
            //
            listening = 0x02,

            // peers
            //  - requests small random sample of peer IP addresses
            //  - no additional data
            //  - heavily throttled to prevent network mapping
            //
            peers = 0x10,

            // ipv4peer/ipv6peer -> ipv4peer/ipv6peer
            //  - broadcasted to others when successfully connected back to an announced peer
            //
            ipv4peer = 0x11,
            ipv6peer = 0x12,

            // identities/channels -> history
            //  - request to update list of identities or channels
            //  - 'history' structure follows with variable number of 'span' members
            //
            identities = 0x20,
            channels = 0x21,
            // threads = 0x22?? or 'download'

            // subscribe -> subscription
            //  - requests subscription to particular 
            //
            subscribe = 0x30,

            // unsubscribe -> eid
            //  - unsubscribes from receiving 'eid' channel
            //
            unsubscribe = 0x31,

            // everything
            //  - requests peer to disregard subscriptions and start sending everything
            //  - this is used by all node participating in network propagation
            //
            everything = 0x32,

            // download -> request::download (eid + uint32_t)
            //  - requests peer to retrieve everything belonging to certain channel or thread
            //    that was created at or after the threshold timestamp
            //  - TODO: empty EID means TOTALLY ALL, but allowed only between core nodes
            //  - if eid is nested post, whole thread gets fetched
            //
            download = 0x33,
        };
        type type : 8;

        // validate
        //  - performs basic validation of protocol frame content, 'size' must be exact
        //
        static bool validate (const void * request, std::size_t size);

        // content
        //  - return pointer to first byte after request header (request content)
        //
        inline void * content () { return this + 1; };
        inline const void * content () const { return this + 1; };

        // max_size
        //  - maximum possible size of the whole request + content
        // 
        static constexpr std::size_t max_size = sizeof (entry) + proof::min_size - 1;

        // max_payload
        //  - maximum possible content size (without request header)
        // 
        static constexpr std::size_t max_payload = max_size - sizeof (std::uint32_t); // sizeof (request)

        // newpeer/ipv4peer/ipv6peer
        //  - newpeer contains common data to IPv4 and IPv6 announcements
        //  - flags: 0x0001 - store as core node (allowed only from other core node)
        //     - unspecified flags are reserved, should be set to zero and ignored
        //  - structure follows request header with type == 'ipv4peer' or 'ipv6peer' respectively
        //
        struct newpeer {
            std::uint16_t port; // little-endian
            std::uint16_t flags;
        };
        struct ipv4peer : newpeer {
            std::uint8_t address [4];
        };
        struct ipv6peer : newpeer {
            std::uint8_t address [16];
        };

        // short_history
        //  - peer requests to receive all relevant data created after 'threshold' timestamp
        //    and everything within each 'span' if the peer has more data than 'number'
        //  - short_history is generic version of 'history' for various requests defined above
        //  - reduction - number of bytes to reduce the history size by (already used in the request content)
        //
        template <std::size_t reduction>
        struct short_history {

            // threshold
            //  - everything equal or newer to this timestamp is requested
            //
            std::uint32_t threshold;

            // flags
            //  - TODO: 0x0001 - if set, peer can (when supported) ask for more details by 'detail_xxx' request
            //  - other bits are reserved for future use and should remain 0 by default
            //
            std::uint16_t flags;

            struct span {

                // threshold
                //  - span threshold, offset 1 (binary value 0x000000 means 1)
                //  - relative to previous span (higher index to 'span' array or topmost 'threshold')
                //  - little endian
                //
                std::uint8_t threshold [3];

                // number
                //  - entries count, offset 1 (binary value 0x000000 means 1)
                //  - 0xFFFFFF means: too many, send 'detail_xyz'
                //  - little endian
                //
                std::uint8_t number [3];
            };

            static constexpr std::size_t header_size = 6;
            static constexpr std::size_t minimal_size = 4;
            static constexpr std::size_t depth = (raddi::request::max_payload - header_size - reduction) / sizeof (struct span);

            struct span span [depth];

        public:
            // length converts 'size' in bytes to 'span' array length
            static constexpr std::size_t length (std::size_t size) {
                if (size > request::history::header_size)
                    return (size - request::history::header_size) / sizeof (struct span);
                else
                    return 0;
            }
            static constexpr bool is_valid_length (std::size_t length) {
                return length < request::history::depth;
            }

            // size converts 'span' array 'length' to size in bytes
            static constexpr std::size_t size (std::size_t length) {
                if (length)
                    return request::history::header_size + length * sizeof (struct span);
                else
                    return request::history::minimal_size;
            }
            static constexpr bool is_valid_size (std::size_t size) {
                if (size < request::history::header_size)
                    return size == request::history::minimal_size;
                else
                    return size <= request::max_payload - reduction
                        && (size - request::history::header_size) % sizeof (struct span) == 0;
            }

            // decode
            //  - decodes 'history' into map of timestamp ranges as keys and numbers of entries within as values
            //
            std::map <std::pair <std::uint32_t, std::uint32_t>, std::uint32_t> decode (std::size_t size) const;
        };

        // history
        //  - peer requests to receive all relevant data created after 'threshold' timestamp
        //    and everything within each 'span' if the peer has more data than 'number'
        //  - size limit of max_payload (133) means 21 ('depth') spans maximum
        //  - TODO: in future, the other side could ask, to break down a particular 'span' (that differs)
        //          in order find better matching
        //
        using history = short_history <0>;

        // subscription
        //  - content following request header with type == 'subscribe'
        //  - peer subscribes to receive entries descending 'channel' (can also be thread, TODO: verify)
        //  - NOTE: after too many subscriptions the peers will just send everything
        //
        struct subscription {
            eid                             channel;
            short_history <sizeof (eid)>    history;

            static constexpr std::size_t size (std::size_t length) {
                return sizeof (eid)
                     + short_history <sizeof (eid)> ::size (length);
            }

            static constexpr std::size_t    depth = decltype (history) ::depth;
            static constexpr std::size_t    minimal_size = sizeof (eid)
                                                         + decltype (request::subscription::history)::minimal_size;
        };
        
        // download
        //  - content following request header with type == 'download'
        //  - peer requests to immediately download all entries that descend 'parent' channel or thread
        //    that were created at equal or later time than 'threshold'
        //
        struct download {
            eid             parent;
            std::uint32_t   threshold;
        };

    };

    // translate
    //  - for passing request::type as a log function parameter
    //
    inline std::wstring translate (enum class request::type type, const std::wstring &) {
        switch (type) {
            case request::type::initial: return L"init";
            case request::type::security_check: return L"security check";
            case request::type::listening: return L"listening";
            case request::type::peers: return L"peers";
            case request::type::ipv4peer: return L"IPv4 peer";
            case request::type::ipv6peer: return L"IPv6 peer";
            case request::type::identities: return L"identities";
            case request::type::channels: return L"channels";
            case request::type::subscribe: return L"subscribe";
            case request::type::everything: return L"everything";
            case request::type::download: return L"download";
            case request::type::unsubscribe: return L"unsubscribe";
        }
        return std::to_wstring ((std::uint8_t) type);
    }
}

#include "raddi_request.tcc"
#endif
