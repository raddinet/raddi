#include "raddi_discovery.h"
#include "raddi_timestamp.h"

namespace {
    std::wstring make_instance_name (short family, std::uint16_t port) {
        wchar_t string [24];
        switch (family) {
            case AF_INET:
                _snwprintf (string, sizeof string / sizeof string [0], L"IPv%u:%u", 4u, port);
                break;
            case AF_INET6:
                _snwprintf (string, sizeof string / sizeof string [0], L"IPv%u:%u", 6u, port);
                break;
            default:
                _snwprintf (string, sizeof string / sizeof string [0], L"%u:%u", family, port);
                break;
        }
        return string;
    }
}

raddi::discovery::discovery (short family, std::uint16_t port)
    : UdpPoint (family, port)
    , provider ("discovery", make_instance_name (family, port)) {};
raddi::discovery::discovery (const SOCKADDR_INET & address)
    : UdpPoint (address)
    , provider ("discovery", raddi::log::translate (address, std::wstring ())) {};

bool raddi::discovery::announce (sockaddr * to, int to_length) {
    if (this->announcement != 0) {
        content data;
        std::memcpy (data.magic, raddi::protocol::magic, sizeof (raddi::protocol::magic));
        data.port = this->announcement;

        if (to) {
            this->report (log::level::note, 0x25, (unsigned int) this->announcement, (const char *) raddi::protocol::magic, (const sockaddr *) to);
            return this->send (&data, sizeof data, to, to_length);
        } else {
            this->report (log::level::note, 0x24, (unsigned int) this->announcement, (const char *) raddi::protocol::magic, (unsigned int) this->port);
            this->history = raddi::now ();
            return this->broadcast (&data, sizeof data, this->port);
        }
    } else
        return false;
}

bool raddi::discovery::start () {
    if (!this->UdpPoint::enable_broadcast ()) {
        this->report (log::level::error, 12, (unsigned int) this->port);
    }
    return this->UdpPoint::start ();
}

void raddi::discovery::packet (std::uint8_t * data_, std::size_t size,
                               sockaddr * from, int from_length) {
    if (this->is_local (from))
        return;
    
    if (size >= sizeof (content)) {
        auto data = reinterpret_cast <content *> (data_);

        if (std::memcmp (data->magic, raddi::protocol::magic, sizeof data->magic) == 0) {
            try {
                auto now = raddi::now ();
                auto & t = this->addresses [from];
                if (t != now) {
                    t = now;
                    this->announce (from, from_length);

                    // using peer's IP address with the port it specified in the announcement
                    //  - 'discovered' adds the address to the coordinator

                    address a (from);
                    a.port = data->port;
                    this->discovered (a);
                }
            } catch (const std::bad_alloc &) {
                this->out_of_memory ();
            }
        } else {
            data->port = 0; // causing NUL terminated magic, to be sure
            this->report (log::level::data, 0x23, (const char *) data->magic, (const char *) raddi::protocol::magic);
        }
    } else
        this->report (log::level::data, 0x24, size, sizeof (content));
}
