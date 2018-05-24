#ifndef RADDI_DISCOVERY_H
#define RADDI_DISCOVERY_H

#include "../node/server.h"
#include "raddi_address.h"
#include "raddi_protocol.h"
#include "raddi_timestamp.h"
#include <map>

namespace raddi {

    // discovery
    //  - local peer discovery broadcasting UDP socket
    // 
    class discovery
        : UdpPoint
        , virtual protected raddi::log::provider <raddi::component::server> {

        std::map <raddi::address, std::uint32_t> addresses;

        // content
        //  - local peer discovery packet content
        //
        struct content {
            char            magic [sizeof (raddi::protocol::magic)];
            std::uint16_t   port;
        };

    public:
        discovery (short family, std::uint16_t port);
        discovery (const SOCKADDR_INET & address);

        // announcement
        //  - TCP port we listen on that is broadcasted to local peers
        //
        std::uint16_t announcement = 0;

        // history
        //  - timestamp of last discovery packed broadcast
        //
        std::uint32_t history = 0;
        
        bool announce (sockaddr * to, int to_length);
        bool announce () { return this->announce (nullptr, 0); }
        bool start ();
        void stop () { return this->UdpPoint::stop (); }

    private:
        bool is_local (const address &);
        void discovered (const address &);
        void out_of_memory ();

        virtual void packet (std::uint8_t * data, std::size_t size,
                             sockaddr * from, int from_length) override;
    };
}

#endif
