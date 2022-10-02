#include "raddi_proof.h"
#include "raddi_timestamp.h"
#include "raddi_eid.h"

#include "../common/log.h"
#include "../common/platform.h"
#include "../common/threadpool.h"
#include "../common/threadpool2.h"

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

        using generator_parent::parallelism;
        using generator_parent::type;
        using generator_parent::operator ();
    };

    // solve
    //  - invokes solver for given complexity/hash
    //  - returns solution length or 0 if no solution was found
    //  - on success returns solution/cycle length
    //
    template <typename Solver>
    std::size_t solve (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES],
                       void * target, std::size_t maximum, cuckoo::parameters parameters) {
        if (parameters.cancel && *parameters.cancel)
            return 0;

        auto solver = std::make_unique <Solver> (parameters);
        return solver->solve (hash, [target, maximum] (std::uintmax_t * cycle, std::size_t length) {

                                        auto size = raddi::proof::size (length);
                                        if (size <= maximum) {

                                            auto raw = reinterpret_cast <std::uint8_t *> (target);
                                            auto proof = reinterpret_cast <raddi::proof *> (&raw [size - 1]);

                                            if (proof->initialize (raddi::proof::algorithm::cuckoo_cycle, Solver::complexity, length)) {
                                                raw [0] = 0x00;  // proof starts with NUL byte

                                                auto solution = proof->solution ();
                                                solution [0] = std::uint32_t (cycle [0]);

                                                for (auto i = 1u; i != length; ++i) {
                                                    solution [i] = std::uint32_t (cycle [i] - cycle [i - 1]);
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
                         void * target, std::size_t maximum, raddi::proof::options options) {

        auto processors = GetLogicalProcessorCount ();

        if (options.parameters.parallelism == 0) {
            options.parameters.parallelism = cuckoo::solver <complexity, generator>::suggested_parallelism; // 64 for 26/27, 128 for 28/29

            if (options.threadpool == raddi::proof::threadpool::automatic) {
                if ((options.parameters.parallelism > 64) && (processors > 64)) {
                    options.threadpool = raddi::proof::threadpool::custom;
                } else {
                    options.threadpool = raddi::proof::threadpool::system;
                }
            }
            if (options.parameters.parallelism > processors) {
                options.parameters.parallelism = processors;
            }
        }

        if (options.parameters.shortest == 0) {
            options.parameters.shortest = raddi::proof::min_length;
        }
        if (options.parameters.longest == 0) {
            options.parameters.longest = raddi::proof::max_length;
        }

        std::size_t length = 0;
        auto t0 = raddi::microtimestamp ();

        switch (options.threadpool) {
            case raddi::proof::threadpool::none:
                length = solve <cuckoo::solver <complexity, generator>> (hash, target, maximum, options.parameters);
                break;

            case raddi::proof::threadpool::system:
                length = solve <cuckoo::solver <complexity, generator, threadpool>> (hash, target, maximum, options.parameters);
                break;

            case raddi::proof::threadpool::custom:
                if ((options.parameters.parallelism > 64) && (GetPredominantSMT () >= 4) && (processors >= 48)) { // threadpool overhead exceeds SMT gains
                    options.parameters.parallelism /= 2;
                }
                length = solve <cuckoo::solver <complexity, generator, threadpool2>> (hash, target, maximum, options.parameters);
                break;
        }

        if (length) {
            auto elapsed = (raddi::microtimestamp () - t0) / 1000;
            if (elapsed < options.requirements.time) {
                raddi::log::note (raddi::component::main, 0x10, complexity, options.requirements.time, elapsed);
                return 0;
            }

            raddi::log::note (raddi::component::main, 0x11, complexity, options.requirements.time, elapsed);
            return raddi::proof::size (length);

        } else {
            raddi::log::note (raddi::component::main, 0x14, complexity);
            return 0;
        }
    }
}

std::size_t raddi::proof::generate (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES],
                                    void * target, std::size_t maximum, options options) {

    static_assert (min_complexity == 26);
    static_assert (max_complexity == 29);

    if (options.requirements.complexity < min_complexity) {
        options.requirements.complexity = min_complexity;
    }

    // complexity and time requriements
    //  - we also bail if time spent exceeds one more second,
    //    better to try another hash than spinning on too hight difficulty

    auto tX = 1000 * (options.requirements.time + 1000);
    auto t0 = raddi::microtimestamp ();
    switch (options.requirements.complexity) {
        case 26:
            if (auto n = attempt <26> (hash, target, maximum, options))
                return n;
            if ((raddi::microtimestamp () - t0) > tX)
                return 0;

            [[ fallthrough ]];
        case 27:
            if (auto n = attempt <27> (hash, target, maximum, options))
                return n;
            if ((raddi::microtimestamp () - t0) > tX)
                return 0;

            [[ fallthrough ]];
        case 28:
            if (auto n = attempt <28> (hash, target, maximum, options))
                return n;
            if ((raddi::microtimestamp () - t0) > tX)
                return 0;

            [[ fallthrough ]];
        case 29:
            options.requirements.time = 0;
            if (auto n = attempt <29> (hash, target, maximum, options))
                return n;

            [[ fallthrough ]];
        default:
            return 0;
    }
}

std::size_t raddi::proof::generate (crypto_hash_sha512_state state, void * target, std::size_t maximum, options options) {
    std::uint8_t hash [crypto_hash_sha512_BYTES];
    if (crypto_hash_sha512_final (&state, hash) == 0)
        return raddi::proof::generate (hash, target, maximum, options);
    else
        return false;
}

/*
std::size_t raddi::proof::generate_wide (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES],
                                         void * target, std::size_t maximum,
                                         options options, volatile bool * cancel) {
    if (options.requirements.complexity < min_complexity) {
        options.requirements.complexity = min_complexity;
    }

    // complexity and time requriements
    //  - we also bail if time spent exceeds one second,
    //    better to try another hash than spinning on too hight difficulty

    auto tX = 1000 * (options.requirements.time + 1000);
    auto t0 = raddi::microtimestamp ();
    switch (options.requirements.complexity) {
        case 26:
            if (auto n = attempt <26> (hash, target, maximum, options, cancel))
                return n;
            if ((raddi::microtimestamp () - t0) > tX)
                return 0;

            [[ fallthrough ]];
        case 27:
            if (auto n = attempt <27> (hash, target, maximum, options, cancel))
                return n;
            if ((raddi::microtimestamp () - t0) > tX)
                return 0;

            [[ fallthrough ]];
        case 28:
            if (auto n = attempt <28> (hash, target, maximum, options, cancel))
                return n;
            if ((raddi::microtimestamp () - t0) > tX)
                return 0;

            [[ fallthrough ]];
        case 29:
            if (auto n = attempt <29> (hash, target, maximum, options, cancel))
                return n;

            [[ fallthrough ]];

        case 30:
            if (auto n = attempt <30> (hash, target, maximum, options, cancel))
                return n;

            [[ fallthrough ]];
        case 31:
            if (auto n = attempt <31> (hash, target, maximum, options, cancel))
                return n;

            [[ fallthrough ]];
        case 32:
            if (auto n = attempt <32> (hash, target, maximum, options, cancel))
                return n;

            [[ fallthrough ]];
        case 33:
            if (auto n = attempt <33> (hash, target, maximum, options, cancel))
                return n;

            [[ fallthrough ]];
        default:
            return 0;
    }
}// */


bool raddi::proof::initialize (enum class algorithm a, std::size_t complexity, std::size_t length) {
    if (a == algorithm::cuckoo_cycle
        && length >= min_length
        && length <= max_length
        && complexity >= min_complexity
        && complexity <= max_complexity) {

        this->algorithm = a;
        this->complexity = complexity - raddi::proof::complexity_bias;
        this->length = (length - raddi::proof::length_bias) / 2;

        return true;
    } else
        return false;
}

std::size_t raddi::proof::validate (std::size_t data_size) const {
    auto proof_size = this->size ();
    if ((this->algorithm == proof::algorithm::cuckoo_cycle) && (data_size >= proof_size))
        return proof_size;
    else
        return 0;
}

bool raddi::proof::verify (const std::uint8_t (&hash) [crypto_hash_sha512_BYTES]) const {
    std::uintmax_t cycle [proof::max_length];

    auto solution = this->solution ();
    auto length = 2 * this->length + this->length_bias;

    cycle [0] = solution [0];
    for (auto i = 1u; i != length; ++i) {
        cycle [i] = cycle [i - 1] + solution [i];
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
