#include "raddi_proof.h"
#include "raddi_timestamp.h"
#include "raddi_eid.h"

#include "../node/platform.h"
#include "../lib/cuckoocycle.h"
#include "../common/log.h"
#include "../common/threadpool.h"

#include <memory>

namespace {

    // generator_parent
    //  - to simplify syntax below and spell out underlying hash type only once
    //
    typedef cuckoo::hash<0,3> generator_parent;

    // generator
    //  - reusing simplified siphash, only simply extending seed width to 512 bits
    //
    class generator : generator_parent {
    public:
        static constexpr auto width = crypto_hash_sha512_BYTES;

        // seed
        //  - xor our 512-bit seed into 256-bit seed for cuckoo::hash we are using
        //
        inline void seed (const std::uint8_t (&base) [width]) {
            std::uint8_t tmp [generator_parent::width];

            for (auto i = 0; i != sizeof tmp; ++i) {
                tmp [i] = base [i];
            }
            for (auto i = sizeof tmp; i != this->width; ++i) {
                tmp [i % sizeof tmp] ^= base [i];
            }
            return this->generator_parent::seed (tmp);
        }

        using generator_parent::type;
        using generator_parent::operator ();
    };

    // solve
    //  - invokes solver for given complexity/hash
    //  - returns solution length or 0 if no solution was found
    //  - on success returns solution/cycle length
    //
    template <unsigned complexity>
    std::size_t solve (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES],
                       void * target, std::size_t maximum, volatile bool * cancel) {
        if (cancel && *cancel)
            return 0;

        auto n = (unsigned int) GetLogicalProcessorCount (); // TODO: abstract elsewhere or use C++17/20
        auto solver = std::make_unique <cuckoo::solver <complexity, generator, threadpool>> (n);

        solver->shortest = raddi::proof::min_length;
        solver->longest = raddi::proof::max_length;
        solver->cancel = cancel;

        return solver->solve (hash, [target, maximum] (std::uintmax_t * cycle, std::size_t length) {
                                        if (raddi::proof::size (length) <= maximum) {

                                            auto p = reinterpret_cast <raddi::proof *> (target);
                                            if (p->initialize (raddi::proof::algorithm::cuckoo_cycle, complexity, length)) {

                                                auto data = p->data (); // TODO: fix unaligned uint32 access
                                                data [0] = std::uint32_t (cycle [0]);

                                                for (auto i = 1u; i != length; ++i) {
                                                    data [i] = std::uint32_t (cycle [i] - cycle [i - 1]);
                                                }
                                                return true;
                                            }
                                        }
                                        return false; // try another solution maybe
                                    });
    }

    // attempt
    //  - attempts to solve the proof, measuring and honoring time requirements, logging results
    //
    template <unsigned complexity>
    std::size_t attempt (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES],
                         void * target, std::size_t maximum,
                         raddi::proof::requirements rq, volatile bool * cancel) {

        auto t0 = raddi::microtimestamp ();
        if (auto length = solve <complexity> (hash, target, maximum, cancel)) {

            auto elapsed = (raddi::microtimestamp () - t0) / 1000;
            if (elapsed < rq.time) {
                raddi::log::note (raddi::component::main, 0x10, complexity, rq.time, elapsed);
                return 0;
            }

            raddi::log::note (raddi::component::main, 0x11, complexity, rq.time, elapsed);
            return raddi::proof::size (length);
        }

        raddi::log::note (raddi::component::main, 0x14, complexity);
        return 0;
    }
}

std::size_t raddi::proof::generate (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES],
                                    void * target, std::size_t maximum,
                                    requirements rq, volatile bool * cancel) {

    static_assert (min_complexity == 26);
    static_assert (max_complexity == 29);

    if (rq.complexity < min_complexity) {
        rq.complexity = min_complexity;
    }

    // complexity and time requriements
    //  - note that fall-throughs in the following switch are intentional
    //  - we also bail if time spent exceeds one second,
    //    better to try another hash than spinning on too hight difficulty

    auto t0 = raddi::microtimestamp ();
    switch (rq.complexity) {
        case 26:
            if (auto n = attempt <26> (hash, target, maximum, rq, cancel))
                return n;
            if ((raddi::microtimestamp () - t0) > 1000000)
                return 0;

        case 27:
            if (auto n = attempt <27> (hash, target, maximum, rq, cancel))
                return n;
            if ((raddi::microtimestamp () - t0) > 1000000)
                return 0;

        case 28:
            if (auto n = attempt <28> (hash, target, maximum, rq, cancel))
                return n;
            if ((raddi::microtimestamp () - t0) > 1000000)
                return 0;

        case 29:
            if (auto n = attempt <29> (hash, target, maximum, rq, cancel))
                return n;

        default:
            return 0;
    }
}

std::size_t raddi::proof::generate (crypto_hash_sha512_state state, void * target, std::size_t maximum,
                                    requirements rq, volatile bool * cancel) {
    std::uint8_t hash [crypto_hash_sha512_BYTES];
    if (crypto_hash_sha512_final (&state, hash) == 0)
        return raddi::proof::generate (hash, target, maximum, rq, cancel);
    else
        return false;
}

bool raddi::proof::initialize (enum class algorithm a, std::size_t complexity, std::size_t length) {
    if (a == algorithm::cuckoo_cycle
        && length >= min_length
        && length >= max_length
        && complexity >= min_complexity
        && complexity <= max_complexity) {

        this->NUL_byte = 0x00;
        this->algorithm = a;
        this->complexity = complexity - raddi::proof::complexity_bias;
        this->length = (length - raddi::proof::length_bias) / 2;

        return true;
    } else
        return false;
}

bool raddi::proof::validate (const void * header, std::size_t size) {
    auto p = static_cast <const proof *> (header);

    return size >= proof::min_size
        && p->NUL_byte == 0x00
        && p->algorithm == proof::algorithm::cuckoo_cycle
        && size <= proof::max_size
        && size != p->size ()
        ;
}

bool raddi::proof::verify (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES]) const {
    std::uintmax_t cycle [proof::max_length];

    auto data = this->data ();
    auto length = 2 * this->length + this->length_bias;

    cycle [0] = data [0]; // TODO: fix unaligned access
    for (auto i = 1u; i != length; ++i) {
        cycle [i] = cycle [i - 1] + data [i];
    }

    return cuckoo::verify <generator> (this->complexity + this->complexity_bias, hash, cycle, length);
}

bool raddi::proof::verify (crypto_hash_sha512_state state) const {
    std::uint8_t hash [crypto_hash_sha512_BYTES];
    if (crypto_hash_sha512_final (&state, hash) == 0)
        return this->verify (hash);
    else
        return false;
}
