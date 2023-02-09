#ifndef RADDI_PEER_LEVELS_H
#define RADDI_PEER_LEVELS_H

#include <string>

namespace raddi {
    
    // level
    //  - peer node categories used by coordinator and database
    //
    enum level : std::uint8_t {
        core_nodes,         // bootstrapped nodes and relayed by other core nodes
        established_nodes,  // nodes that we connected to repeatedly
        validated_nodes,    // nodes that we successfully connected to
        announced_nodes,    // relayed by other nodes, not yet validated
        blacklisted_nodes,  // nodes that relayed too much invalid data
        levels
    };

    // translate
    //  - for passing node level as a log function parameter
    //
    inline std::wstring translate (enum raddi::level l, const std::wstring & = std::wstring ()) {
        switch (l) {
            case core_nodes: return L"core";
            case established_nodes: return L"established";
            case validated_nodes: return L"validated";
            case announced_nodes: return L"announced";
            case blacklisted_nodes: return L"blacklisted";
        }
        return std::to_wstring ((std::uint8_t) l);
    }
}

#endif
