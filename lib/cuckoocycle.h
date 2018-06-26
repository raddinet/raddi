#ifndef CUCKOOCYCLE_H
#define CUCKOOCYCLE_H

#include <cstddef>
#include <cstdint>

#include <type_traits>
#include <algorithm>
#include <vector>
#include <bitset>

// cuckoo
//  - TODO: reinterpret_cast instead of C style
//  - TODO: combine common parts of trimXxxx functions
//  - TODO: move common operations to indexer
//  - TODO: make solver->base /buckets class with 'write<N>' function
//  - TODO: use libsimdpp for SipHash and other
//
namespace cuckoo {

    // singlethreaded
    //  - fallback thread pool controller; single-threaded serial
    //
    template <typename T>
    class singlethreaded {
    public:
        void begin (std::size_t) {};
        bool dispatch (void (T::*fn)(), T * t) {
            (t->*fn) ();
            return true;
        };
        void join () {};
    };

    // hash
    //  - parametrizable version of modofied SipHash function simplified for fast graph edge generation
    //
    template <unsigned N1, unsigned N2>
    class hash {
        std::uint64_t base [4];

    public:
        // type
        //  - hash input/output type, must be at least Complexity-bits wide
        //
        typedef std::uint64_t type;

        // width
        //  - required seed size in bytes
        //  - 
        //
        static constexpr auto width = sizeof (hash::base);

        // parallelism
        //  - defines how many hashes does parallel operator() compute at once
        //
        static constexpr auto parallelism = 1u;

        // seed
        //  - initialization function
        //
        inline void seed (const std::uint8_t (&base) [width]) {
            std::memcpy (this->base, base, width);
        }

        // operator ()
        //  - generates either one or full parallelism-sized batch of outputs per input(s)
        //
        inline type operator () (type input) const {
            std::uint64_t v [4] = {
                this->base [0],
                this->base [1] ^ (input * !N1),
                this->base [2],
                this->base [3] ^ (input * !!N2),
            };

            for (auto i = 0u; i != N1; ++i) {
                round (v);
            }
            if (N1 && N2) {
                v [0] ^= input;
                v [2] ^= 0xff;
            }
            for (auto i = 0u; i != N2; ++i) {
                round (v);
            }
            return (v [0] ^ v [1]) ^ (v [2] ^ v [3]);
        }
        inline void operator () (type (&output) [parallelism], const type (&input) [parallelism]) const {
            // TODO: implement SSE2/AVX version
            for (std::size_t i = 0u; i != this->parallelism; ++i) {
                output [i] = this->operator () (input [i]);
            }
        }

    private:
        static inline std::uint64_t rotl64 (const std::uint64_t x, const int b) {
            return (x << b) | (x >> (64 - b));
        }
        static inline void round (std::uint64_t (&v) [4]) {
            v [0] += v [1];
            v [1] = rotl64 (v [1], 13);
            v [1] ^= v [0];
            v [0] = rotl64 (v [0], 32);
            v [2] += v [3];
            v [3] = rotl64 (v [3], 16);
            v [3] ^= v [2];
            v [0] += v [3];
            v [3] = rotl64 (v [3], 21);
            v [3] ^= v [0];
            v [2] += v [1];
            v [1] = rotl64 (v [1], 17);
            v [1] ^= v [2];
            v [2] = rotl64 (v [2], 32);
        }
    };


    // verify
    //  - Generator - edges hashing functor; verification does not use parallel generation
    //  - complexity/seed
    //     - must be the same used to find solution, to successfully verify it
    //  - cycle/length - solution to verify
    //
    template <typename Generator>
    bool verify (unsigned complexity, const std::uint8_t (&seed) [Generator::width], const std::uintmax_t * cycle, std::size_t length);


    // solver
    //  - modified matrix/mean solver
    //  - Complexity
    //     - graph node/edge size in bits
    //     - NOTE: tested working values are: 24, 25, 26, 27, 28 and 29
    //     - NOTE: memory usage for 27: 900 MB, for 28: 2200 MB, etc.
    //  - Generator
    //     - hash or stable functor used to generate edges, se hash above
    //     - see definition of 'hash' above for details how it should be implemented
    //  - ThreadPoolControl
    //     - platform-specific class to dispatch work into threadpool
    //     - see definition of 'singlethreaded' above for contract
    //
    template <unsigned Complexity,
              typename Generator = cuckoo::hash <2,4>,
              template <typename> class ThreadPoolControl = singlethreaded>
    class solver {
    public:

        // cancel
        //  - set 'cancel' to true during search for 'solve' to terminate prematurely
        //  - NOTE: I know 'volatile' should be pointless here ...but just to be sure
        //  - does not reset automatically to prevent races
        //
        volatile bool cancel = false;

        // shortest/longest
        //  - length limits imposed on solutions
        //  - shortest than 4 and longest than MAXPATHLEN won't work
        //  - solutions with length outside this range are discarded
        //
        unsigned int shortest = 4;
        unsigned int longest = MAXPATHLEN;

        // solve
        //  - finds cycles/solutions and appends them to 'solutions' vector above
        //  - returns number of cycles/solutions found with valid length
        //  - parameters: seed - hash or any blob of data that seeds the graph
        //                callback - bool callback (std::uintmax_t * cycle, std::size_t length)
        //                         - return true to stop searching, false to continue
        //
        template <typename Callback>
        std::size_t solve (const std::uint8_t (&seed) [Generator::width], Callback callback);

    private:
        typedef typename std::conditional <(Complexity >= 30), std::uint64_t, std::uint32_t>::type offset;

        static constexpr auto XBITS = (Complexity / 2u) - 7u; // 6 for 26 & 27, 7 for 28 & 29
        static constexpr auto YBITS = XBITS; // TODO: I rely on these two being same (thread::start/end)
        static constexpr auto BIGSIZE = 5u;
        static constexpr auto BIGSIZE0 = (Complexity < 30) ? 4 : BIGSIZE;
        static constexpr auto NEEDSYNC = (Complexity > 27) && (Complexity < 30);
        static constexpr auto COMPRESSROUND = 28u; // EVEN!, TODO: was 14 for 28  ???
        static constexpr auto EXPANDROUND = COMPRESSROUND;

        static constexpr auto SMALLSIZE = 5u;
        static constexpr auto BIGGERSIZE = (EXPANDROUND == COMPRESSROUND) ? BIGSIZE : (BIGSIZE + 1);

        static constexpr auto NEDGES = 1u << Complexity;
        static constexpr auto EDGEMASK = NEDGES - 1u;
        static constexpr auto MAXPATHLEN = 8u << ((Complexity + 3) / 3);

        static constexpr auto NX = 1u << XBITS;
        static constexpr auto XMASK = NX - 1;
        static constexpr auto NY = 1u << YBITS;
        static constexpr auto YMASK = NY - 1;
        static constexpr auto XYBITS = XBITS + YBITS;
        static constexpr auto NXY = 1u << XYBITS;
        static constexpr auto ZBITS = Complexity - XYBITS;
        static constexpr auto NZ = 1u << ZBITS;
        static constexpr auto ZMASK = NZ - 1;
        static constexpr auto YZBITS = Complexity - XBITS;
        static constexpr auto NYZ = 1u << YZBITS;
        static constexpr auto YZMASK = NYZ - 1;
        static constexpr auto YZ1BITS = (YZBITS < 15) ? YZBITS : 15;
        static constexpr auto NYZ1 = 1u << YZ1BITS;
        static constexpr auto YZ1MASK = NYZ1 - 1u;
        static constexpr auto Z1BITS = YZ1BITS - YBITS;
        static constexpr auto NZ1 = 1u << Z1BITS;
        static constexpr auto Z1MASK = NZ1 - 1;
        static constexpr auto YZ2BITS = (YZBITS < 11) ? YZBITS : 11;
        static constexpr auto NYZ2 = 1u << YZ2BITS;
        static constexpr auto YZ2MASK = NYZ2 - 1;
        static constexpr auto Z2BITS = YZ2BITS - YBITS;
        static constexpr auto NZ2 = 1u << Z2BITS;
        static constexpr auto Z2MASK = NZ2 - 1;
        static constexpr auto YZZBITS = YZBITS + ZBITS;
        static constexpr auto YZZ1BITS = YZ1BITS + ZBITS;

        static constexpr auto BIGSLOTBITS = BIGSIZE * 8u;
        static constexpr auto SMALLSLOTBITS = SMALLSIZE * 8u;
        static constexpr auto BIGSLOTMASK = (1uLL << BIGSLOTBITS) - 1uLL;
        static constexpr auto SMALLSLOTMASK = (1uLL << SMALLSLOTBITS) - 1uLL;
        static constexpr auto BIGSLOTBITS0 = BIGSIZE0 * 8u;
        static constexpr auto BIGSLOTMASK0 = (1uLL << BIGSLOTBITS0) - 1uLL;
        static constexpr auto NONYZBITS = BIGSLOTBITS0 - YZBITS;
        static constexpr auto NNONYZ = 1u << NONYZBITS;

        static constexpr auto NTRIMMEDZ = NZ * 176u / 256u;

        static constexpr auto ZBUCKETSLOTS = NZ + NZ * 3u / 64u;
        static constexpr auto ZBUCKETSIZE = ZBUCKETSLOTS * BIGSIZE0;
        static constexpr auto TBUCKETSIZE = ZBUCKETSLOTS * BIGSIZE;

        template <unsigned Size>
        struct zbucket {
            union alignas (2 * sizeof (std::size_t)) {
                std::uint8_t    bytes [Size];
                std::uint32_t   words [Size / sizeof (std::uint32_t)];
                struct {
                    std::uint32_t padding_ [Size / sizeof (std::uint32_t) - (2 * NZ2 + 2 * NZ1)];
                    std::uint32_t renameu1 [NZ2];
                    std::uint32_t renamev1 [NZ2];
                    std::uint32_t renameu [NZ1];
                    std::uint32_t renamev [NZ1];
                };
            };
            std::size_t size;

            std::size_t setsize (const std::uint8_t * end) {
                this->size = end - this->bytes;
                return this->size;
            }
        };

        template <unsigned Size>
        using yzbucket = zbucket <Size> [NY];

        template <unsigned Size>
        struct indexer {
            offset index [NX];

            void initV (std::size_t y);
            void initU (std::size_t x);

            std::size_t storeV (yzbucket <Size> * buckets, std::size_t y);
            std::size_t storeU (yzbucket <Size> * buckets, std::size_t x);
        };

        class thread {
            yzbucket <TBUCKETSIZE> bucket;
            std::uint8_t  deg  [2 * NYZ1];
            std::uint16_t z    [NTRIMMEDZ];
            std::uint32_t edge [NTRIMMEDZ];

        public:
            solver *    solver;
            std::size_t start;
            std::size_t end;

            void match ();
            void genUnodes ();
            void genVnodes ();

            template <bool TRIMONV> void trimRound ();
            template <bool TRIMONV> void trimRename1 ();

        private:
            template <bool TRIMONV> void trimEdges1 ();
            template <unsigned SRCSIZE, unsigned DSTSIZE, bool TRIMONV> void trimEdges ();
            template <unsigned SRCSIZE, unsigned DSTSIZE, bool TRIMONV> void trimRename ();
        };

    private:
        Generator                   generator;
        ThreadPoolControl <thread>  threadpool;

        union {
            yzbucket <ZBUCKETSIZE> * const buckets;
            std::uint8_t *           const base;
        };
        /* TODO: when (MSVC C2148 on complexity 29) possible: union {
            yzbucket <ZBUCKETSIZE> buckets [NX];
            std::uint8_t           base [NX * sizeof (yzbucket <ZBUCKETSIZE>)];
        };// */

        std::vector <thread>    threads;
        std::bitset <NXY>       uxymap;
        std::uintmax_t          cycleus [MAXPATHLEN];
        std::uintmax_t          cyclevs [MAXPATHLEN];
        std::uintmax_t          solution [MAXPATHLEN];
        std::size_t             length;
        std::size_t             round;
        std::uint32_t           results [2 * NX * NYZ2];

    public:
        explicit solver (unsigned int parallelism)
            : buckets (new yzbucket <ZBUCKETSIZE> [NX]) {

            try {
                this->threads.resize (parallelism);
            } catch (const std::bad_alloc &) {
                delete [] this->buckets;
            }
        };
        ~solver () {
            delete [] this->buckets;
        }

    public:
        static constexpr auto               complexity = Complexity;
        typedef Generator                   generator_type;
        typedef ThreadPoolControl <thread>  threadpool_type;

    private:
        std::uint32_t path (std::uint32_t * cycle, std::uint32_t u, std::uint32_t * us) const;
        void recordedge (unsigned int i, unsigned int u2, unsigned int v2);

        // touch
        //  - attempts to bring all commited pages into working set for improved performance
        //
        static void touch (void * p, std::size_t n, volatile bool * cancel = nullptr);
    };
}

#include "cuckoocycle.tcc"
#endif
