#include "raddi_request.h"
#include "raddi_address.h"
#include "raddi_timestamp.h"
#include "raddi_consensus.h"

#include "../common/log.h"

// NOTE: validate/verify strings are in raddi_database.rc, "DATABASE | DATA | 0x2?" rows

namespace {
    template <std::uint16_t AF, typename T>
    bool validate_ipXpeer_request (const raddi::request * r, std::size_t length) {
        if (length == sizeof (raddi::request) + sizeof (T)) {

            raddi::address peer;
            peer.family = AF;
            peer.port = static_cast <const T *> (r->content ())->port;

            switch (AF) {
                case AF_INET:
                    std::memcpy (&peer.address4, static_cast <const T *> (r->content ())->address, 4);
                    return peer.valid ();
                case AF_INET6:
                    std::memcpy (&peer.address6, static_cast <const T *> (r->content ())->address, 16);
                    return peer.valid ();
            }
            return false;
        } else
            return raddi::log::data (raddi::component::database, 0x23,
                                     r->type, length, sizeof (raddi::request) + sizeof (T));
    }
}

bool raddi::request::validate (const void * header, std::size_t length) {
    if (length < sizeof (request) || length > request::max_size)
        return false;

    const auto r = static_cast <const request *> (header);
    const auto timestamp = (raddi::now () & 0xFF000000) | r->mark;

    if (raddi::older (timestamp, raddi::now () - raddi::consensus::max_request_age_allowed)) {
        return raddi::log::data (raddi::component::database, 0x20, raddi::now () - raddi::consensus::max_request_age_allowed,
                                 timestamp, raddi::consensus::max_request_age_allowed, raddi::now () - timestamp);
    }
    if (raddi::older (raddi::now () + raddi::consensus::max_request_skew_allowed, timestamp)) {
        return raddi::log::data (raddi::component::database, 0x21, raddi::now () + raddi::consensus::max_request_skew_allowed,
                                 timestamp, raddi::consensus::max_request_skew_allowed, timestamp - raddi::now ());
    }

    switch (r->type) {
        case request::type::initial:
            return length == sizeof (request) + sizeof raddi::protocol::magic
                || raddi::log::data (raddi::component::database, 0x23, r->type, length, sizeof (request) + sizeof raddi::protocol::magic);

        // case request::type::security_check:
        //    return ...;

        case request::type::listening:
            return length == sizeof (request) + sizeof (std::uint16_t)
                || raddi::log::data (raddi::component::database, 0x23, r->type, length, sizeof (request) + sizeof (std::uint16_t));

        case request::type::peers:
            return length == sizeof (request)
                || raddi::log::data (raddi::component::database, 0x23, r->type, length, sizeof (request));

        case request::type::ipv4peer:
            return validate_ipXpeer_request <AF_INET, request::ipv4peer> (r, length);
        case request::type::ipv6peer:
            return validate_ipXpeer_request <AF_INET6, request::ipv6peer> (r, length);


        case request::type::identities:
        case request::type::channels:
            if (length >= sizeof (request) + request::history::minimal_size) {
                auto content = static_cast <const request::history *> (r->content ());

                if (!content->is_valid_size (length - sizeof (request)))
                    return raddi::log::data (raddi::component::database, 0x23, r->type, length, L"4||10+n×6");

                if (content->threshold && raddi::older (content->threshold, raddi::now () - 0x70000000u)) // TODO: 0x70000000u -> settings
                    return raddi::log::data (raddi::component::database, 0x24,
                                             r->type, content->threshold, raddi::now () - 0x70000000u, 0x70000000u / (60 * 60 * 24));
                
                // TODO: if (content->threshold == 0) then (content->length (length - sizeof (request)) must be also 0

                return true;
            } else
                return raddi::log::data (raddi::component::database, 0x23, r->type, length, sizeof (request) + request::history::minimal_size);

       case request::type::subscribe:
            if (length >= sizeof (request) + request::subscription::minimal_size) {
                auto content = static_cast <const request::subscription *> (r->content ());

                if (!content->history.is_valid_size (length - sizeof (request)))
                    return raddi::log::data (raddi::component::database, 0x23, r->type, length, L"20||26+n×6");

                if (raddi::older (content->history.threshold, raddi::now () - 0x02000000u)) // TODO: 0x02000000u -> settings
                    return raddi::log::data (raddi::component::database, 0x24,
                                             r->type, content->history.threshold, raddi::now () - 0x02000000u, 0x02000000u / (60 * 60 * 24));
                return true;
            } else
                return raddi::log::data (raddi::component::database, 0x23,
                                         r->type, length, sizeof (request) + request::subscription::minimal_size);
        
        case request::type::everything:
            return length == sizeof (request)
                || raddi::log::data (raddi::component::database, 0x23, r->type, length, sizeof (request));

        case request::type::unsubscribe:
            return length == sizeof (request) + sizeof (eid)
                || raddi::log::data (raddi::component::database, 0x23, r->type, length, sizeof (request) + sizeof (eid));

        case request::type::download:
            return length == sizeof (request) + sizeof (download)
                || raddi::log::data (raddi::component::database, 0x23, r->type, length, sizeof (request) + sizeof (download));

        default:
            // unknown requests are allowed (but ignored) for forward compatibility
            raddi::log::data (raddi::component::database, 0x22, (unsigned int) r->type);
            return true;
    }
}
