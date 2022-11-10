#ifndef RADDI_PROOF_H
#define RADDI_PROOF_H

#include "raddi_eid.h"
#include <sodium.h>

#include "../lib/cuckoocycle.h"

namespace raddi {

    // proof of work
    //  - represents proof-of-work found within an entry (last byte of the entry)
    //     - POW begins with NUL byte and ends with 'proof' structure (byte)
    //  - proof-of-work validates the entry through dynamically chosen complexity
    //  - appended to entry content and signed along with the rest
    //  - currently cuckoo cycle algorithm, may be extended later
    //  - NOTE: on ARMv7 unaligned memory access is OK (with performance penalty)
    //  - TODO: simplify somehow, too much raw pointer arithmetics here
    //
    struct proof __attribute__ ((ms_struct)) {

        // length/complexity bits
        //  - defines how many bits in proof's header bitfield these values take

        static constexpr auto length_bits = 4u;
        static constexpr auto complexity_bits = 2u;

        // length/complexity bias
        //  - lowest allowed values which match the actual transmitted values of 0

        static constexpr auto length_bias = 12u;
        static constexpr auto complexity_bias = 26u;

        // min/max length/complexity
        //  - computed ranges of possible values

        static constexpr auto min_complexity = complexity_bias;
        static constexpr auto max_complexity = min_complexity + (1 << complexity_bits) - 1;
        static constexpr auto max_wide_complexity = min_complexity + (1 << (complexity_bits + 1)) - 1;
        static constexpr auto min_length = length_bias;
        static constexpr auto max_length = min_length + 2 * ((1 << length_bits) - 1);

        // algorithm
        //  - 
        //  - NOTE: use 'reserved11' first, we might choose to use bit 6 for complexity
        //
        enum class algorithm : std::uint8_t {
            reserved00 = 0,
            reserved01 = 1,
            cuckoo_cycle = 2,
            reserved11 = 3,
        };

        // data

        std::uint8_t length     : length_bits;     // 12,14,16,18, 20,22,24,26, 28,30,32,34, 36,38,40,42
        std::uint8_t complexity : complexity_bits; // 26,27,28,29 (29 requires more than 2 GB of memory)
        algorithm    algorithm  : 2;

        // initialize
        //  - applies bias to provided parameters and sets header members (above)
        //  - returns false if any parameter is invalid, true otherwise
        //
        bool initialize (enum class algorithm, std::size_t complexity, std::size_t length);

        // solution
        //  - representation of the solution
        //  - return pointer to first byte/uint32 after proof's initial NUL byte
        //
        inline std::uint32_t * solution () {
            return reinterpret_cast <std::uint32_t *> (
                reinterpret_cast <std::uint8_t *> (this - this->size () + sizeof (proof) + sizeof (std::uint8_t)));
        };
        inline const std::uint32_t * solution () const {
            return reinterpret_cast <const std::uint32_t *> (
                reinterpret_cast <const std::uint8_t *> (this - this->size () + sizeof (proof) + sizeof (std::uint8_t)));
        };

        // data
        //  - return pointer to first byte (the NUL byte) of the proof
        //
        inline std::uint8_t * data () {
            return reinterpret_cast <std::uint8_t *> (this - this->size () + sizeof (proof));
        };
        inline const std::uint8_t * data () const {
            return reinterpret_cast <const std::uint8_t *> (this - this->size () + sizeof (proof));
        };

        // size
        //  - expression that returns proof size for given length
        //  - initial NUL byte, solution nonces, terminating control byte (this)
        //
        static constexpr std::size_t size (std::size_t length) {
            return sizeof (std::uint8_t) + (sizeof (std::uint32_t) * length) + sizeof (proof);
        }

        // min/max size
        //  - the limits of size of the whole proof

        static constexpr auto min_size = 2u + sizeof (std::uint32_t) * min_length; // raddi::proof::size (min_length);
        static constexpr auto max_size = 2u + sizeof (std::uint32_t) * max_length; // raddi::proof::size (max_length);

        // requirements
        //  - minimal search parameters, complexity and time in milliseconds
        //  - 'generate' will either satisfy all parameters or fail
        //
        struct requirements {
            unsigned int complexity = min_complexity;
            unsigned int time = 500; // ms
        };

        // threadpool
        //  - which threadpool to use when parallelizing workload
        //
        enum class threadpool : std::uint8_t {
            automatic = 0,
            none,   // v0 "cuckoocycle.h" no threadpool
            system, // v1 "threapool.h" QueueUserWorkItem
            custom, // v2 "threapool2.h" spans groups, but has higher overhead
        };

        // options
        //  - cummulative parameter to configure proof generator optional settings
        //
        struct options {
            requirements        requirements;
            threadpool          threadpool = threadpool::automatic;
            cuckoo::parameters  parameters; // PoW generator parameters
        };

    public:

        // validate
        //  - performs basic validation of the proof byte
        //  - 'size' must be exact the size of the entry CONTENT containing the proof
        //  - returns proof size (in bytes) on success, 0 when invalid
        //
        std::size_t validate (std::size_t size) const;

        // generate
        //  - attempts to generate proof-of-work from the hash into 'target' (including this header)
        //  - time requirements makes this future-proof for times when most devices can compute
        //    the cycle much faster and nodes thus require higher complexity than now
        //     - function will try increasingly higher difficulty to honor time requirements
        //     - function will not try higher difficulty if it took more than 1s (try different hash)
        //  - returns number of bytes written to 'target' or 0 if no proof could be found
        //     - NOTE: it's normal (50% chance) that no proof can be found,
        //             just change hash (increase timestamp) and try again
        //
        static std::size_t generate (crypto_hash_sha512_state, void * target, std::size_t maximum, options);
        static std::size_t generate (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES], void * target, std::size_t maximum, options);

        // static std::size_t generate_wide (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES], void * target, std::size_t maximum,
        //                                  options, volatile bool * cancel = nullptr);

        // size
        //  - returns full size of this 'proof' structure, including header, in bytes
        //
        std::size_t size () const {
            return this->size (2 * this->length + proof::length_bias);
        }

        // verify
        //  - verifies that 'data' cycle is found in cycle derived from hash
        //
        bool verify (crypto_hash_sha512_state) const;
        bool verify (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES]) const;
    };
}

#endif
