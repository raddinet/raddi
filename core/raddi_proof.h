#ifndef RADDI_PROOF_H
#define RADDI_PROOF_H

#include "raddi_eid.h"
#include <sodium.h>

namespace raddi {

    // proof of work
    //  - on-wire and in-memory format of a proof-of-work header
    //  - proof-of-work validates the entry through dynamically chosen complexity
    //  - appended to entry content and signed along with the rest
    //  - currently cuckoo cycle algorithm, may be extended later
    //  - NOTE: ARM: unaligned memory access here and in cuckoocycle.tcc !!!
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
        static constexpr auto min_length = length_bias;
        static constexpr auto max_length = min_length + 2 * ((1 << length_bits) - 1);

        // algorithm
        //  - 
        //
        enum class algorithm : std::uint8_t {
            reserved00 = 0,
            reserved01 = 1,
            cuckoo_cycle = 2,
            reserved11 = 3,
        };

        // header
        //  - 2 byte header that preceeds cuckoo cycle solution
        //  - NUL byte closes the entry content and introduces the proof
        //     - see parsing rules in raddi::entry::proof ()

        std::uint8_t NUL_byte;
        std::uint8_t length     : length_bits;     // 12,14,16,18, 20,22,24,26, 28,30,32,34, 36,38,40,42
        std::uint8_t complexity : complexity_bits; // 26,27,28,29 (29 requires more than 2 GB of memory)
        algorithm    algorithm  : 2;

        // initialize
        //  - applies bias to provided parameters and sets header members (above)
        //  - returns false if any parameter is invalid, true otherwise
        //
        bool initialize (enum class algorithm, std::size_t complexity, std::size_t length);

        // data
        //  - representation of the solution
        //  - the actual size for given 'length' can be determined by calling .size()
        //  - return pointer to first byte after proof header
        //
        inline std::uint32_t * data () { return reinterpret_cast <std::uint32_t *> (this + 1); };
        inline const std::uint32_t * data () const { return reinterpret_cast <const std::uint32_t *> (this + 1); };

        // size
        //  - expression that returns proof size for given length
        //
        static constexpr std::size_t size (std::size_t length) {
            return sizeof (proof) + sizeof (std::uint32_t) * length;
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

    public:

        // validate
        //  - performs basic validation of proof content and size, 'size' must be exact
        //
        static bool validate (const void * proof, std::size_t size);

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
        static std::size_t generate (crypto_hash_sha512_state, void * target, std::size_t maximum,
                                     requirements, volatile bool * cancel = nullptr);
        static std::size_t generate (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES], void * target, std::size_t maximum,
                                     requirements, volatile bool * cancel = nullptr);

        // size
        //  - returns full size of this 'proof' structure, including header, in bytes
        //
        std::size_t size () const {
            // sizeof (proof) + sizeof (std::uint32_t) * (2 * this->length + length_bias);
            return this->size (2 * this->length + this->length_bias);
        }

        // verify
        //  - verifies that 'data' cycle is found in cycle derived from hash
        //
        bool verify (crypto_hash_sha512_state) const;
        bool verify (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES]) const;
    };
}

#endif
