#ifndef RADDI_CONSENSUS_H
#define RADDI_CONSENSUS_H

#include <cstdint>

namespace raddi {
    namespace consensus {
        static constexpr std::uint32_t max_entry_skew_allowed = 180; // seconds into the future
        static constexpr std::uint32_t max_entry_age_allowed = 600; // seconds old for network propagation

        static constexpr std::uint32_t max_request_skew_allowed = 180; // seconds into the future
        static constexpr std::uint32_t max_request_age_allowed = 240; // seconds old

        // max_xxx_name_size
        //  - restriction in naming new identities and channels serve several purposes:
        //     - easier and faster search
        //     - reduce potential in harmful payload
        //     - reduce storage requirements, especially for shard indexes

        static constexpr std::uint32_t max_identity_name_size = 53; // in bytes, currently limited to 53
        static constexpr std::uint32_t max_channel_name_size = 85; // in bytes, currently limited to 85

        // announement/entry complexity/time requirements

        static constexpr std::uint32_t min_entry_pow_time = 500;
        static constexpr std::uint32_t min_entry_pow_complexity = 26;
        static constexpr std::uint32_t min_announcement_pow_time = 1500;
        static constexpr std::uint32_t min_announcement_pow_complexity = 27;
    }
}

#endif
