#ifndef RADDI_COMMAND_H
#define RADDI_COMMAND_H

#include "raddi_entry.h"
#include "../common/log.h"
#include "../common/uuid.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace raddi {

    // command
    //  - client application command header
    //  - passed to service through Source directory
    //  - payload follows
    // 
    struct command {

        // type
        //  - request type code
        //  - extended to 32-bit for content alignment
        //
        enum class type : std::uint32_t {

            test                = 0x00, // no data
            set_log_level       = 0x01, // data: raddi::log::level (int)
            set_display_level   = 0x02, // data: raddi::log::level (int)
            optimize            = 0x03, // no data

            // TODO: query_more_peers?
            // TODO: listen/stop(listening)? NOTE: add locking in coordinator (listeners list) when implementing this
            // TODO: discovery(new/stop)? NOTE: also needs the locking

            add_peer            = 0x10, // data: 'address'
            rem_peer            = 0x11, // data: 'address'
            ban_peer            = 0x12, // data: 'address' ...TODO: + optionally uint16_t (days)
            unban_peer          = 0x13, // data: 'address'
            add_core_peer       = 0x1A, // data: 'address'
            connect_peer        = 0x1C, // data: 'address'

            // data tables: 0x2x

            download            = 0x20, // data: 'raddi::request::download'
            erase               = 0x21, // data: 'eid'
            erase_thorough      = 0x22, // data: 'eid'

            // subscriptions to channels

            subscribe           = 0x30, // data: 'subscription'
            unsubscribe         = 0x31, // data: 'subscription'
            blacklist           = 0x32, // data: 'subscription'
            unblacklist         = 0x33, // data: 'subscription'
            retain              = 0x34, // data: 'subscription'
            unretain            = 0x35, // data: 'subscription'

        } type;

        struct subscription {
            eid  channel;
            uuid application;
        };

        // content
        //  - return pointer to first byte after request header (request content)
        //
        inline void * content () { return this + 1; };
        inline const void * content () const { return this + 1; };
        
        // max_size
        //  - maximum possible size of the whole request + content
        // 
        static constexpr std::size_t max_size = sizeof (entry) + proof::min_size - 1;
    };

    // ensure data structures are valid size

    static_assert (sizeof (command) + sizeof (command::subscription) <= command::max_size);

    // translate
    //  - for passing request::type as a log function parameter
    //
    inline std::wstring translate (enum class command::type type, const std::wstring &) {
        switch (type) {
            case command::type::test: return L"test";
            case command::type::set_log_level: return L"set log level";
            case command::type::set_display_level: return L"set display level";
            case command::type::optimize: return L"optimize";

            case command::type::add_peer: return L"add peer";
            case command::type::rem_peer: return L"remove peer";
            case command::type::ban_peer: return L"ban peer";
            case command::type::unban_peer: return L"unban peer";
            case command::type::add_core_peer: return L"add core peer";
            case command::type::connect_peer: return L"connect peer";

            case command::type::download: return L"download";
            case command::type::erase: return L"erase";
            case command::type::erase_thorough: return L"thorough erase";

            case command::type::subscribe: return L"subscribe";
            case command::type::unsubscribe: return L"unsubscribe";
            case command::type::blacklist: return L"blacklist";
            case command::type::unblacklist: return L"unblacklist";
            case command::type::retain: return L"retain";
            case command::type::unretain: return L"unretain";
        }
        return std::to_wstring ((std::uint8_t) type);
    }
}

#endif
