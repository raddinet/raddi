#ifndef CUCKOOCYCLE_TCC
#define CUCKOOCYCLE_TCC

#include "cuckoocycle.h"

// hash

template <unsigned N1, unsigned N2>
inline std::uint64_t cuckoo::hash <N1, N2> ::rotl64 (const std::uint64_t x, const int b) {
    return (x << b) | (x >> (64 - b));
}
template <unsigned N1, unsigned N2>
inline void cuckoo::hash <N1, N2> ::round (std::uint64_t (&v) [4]) {
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

template <unsigned N1, unsigned N2>
inline void cuckoo::hash <N1, N2> ::seed (const std::uint8_t (&base) [width]) {
    std::memcpy (this->base, base, width);
}
template <unsigned N1, unsigned N2>
inline void cuckoo::hash <N1, N2> ::seed (const std::uint8_t (&base) [width], const std::uint8_t * key, std::size_t length) {
    std::uint8_t seed [width];
    std::size_t i = 0;

    if (length > width) {
        length = width;
    }
    for (; i != length; ++i) {
        seed [i] = base [i] ^ key [i];
    }
    for (; i != width; ++i) {
        seed [i] = base [i];
    }
    this->seed (seed);
}

template <unsigned N1, unsigned N2>
inline typename cuckoo::hash <N1, N2> ::type cuckoo::hash <N1, N2> ::operator () (type input) const {
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

template <unsigned N1, unsigned N2>
inline typename cuckoo::hash <N1, N2> ::type cuckoo::hash <N1, N2> ::operator () (const void * data, std::size_t size) const {
    auto input = reinterpret_cast <const type *> (data);
    auto cycles = size / sizeof (type);
    type final = type (size) << 56;

    std::uint64_t v [4] = { this->base [0], this->base [1], this->base [2], this->base [3] };

    if (input && cycles) {
        v [1] ^= (*input * !N1);
        v [3] ^= (*input * !!N2);

        for (; cycles; ++input, --cycles) {
            v [3] ^= *input;
            for (auto i = 0u; i != N1; ++i) {
                round (v);
            }
            v [0] ^= *input;
        }
    }

    if (auto remaining = size % 8) {
        auto end = reinterpret_cast <const std::uint8_t *> (data) + size - remaining;
        switch (remaining) {
            case 7: final |= type (end [6]) << 48; // continue...
            case 6: final |= type (end [5]) << 40; // continue...
            case 5: final |= type (end [4]) << 32; // continue...
            case 4: final |= type (end [3]) << 24; // continue...
            case 3: final |= type (end [2]) << 16; // continue...
            case 2: final |= type (end [1]) << 8; // continue...
            case 1: final |= type (end [0]);
        }
    }

    if (N1 || N2) {
        if (N1) {
            v [3] ^= final;
            for (auto i = 0u; i != N1; ++i) {
                round (v);
            }
        }
        v [0] ^= final;
        v [2] ^= 0xff;
    }
    for (auto i = 0u; i != N2; ++i) {
        round (v);
    }
    return (v [0] ^ v [1]) ^ (v [2] ^ v [3]);
}

// indexer

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <unsigned Size>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::indexer <Size> ::initV (std::size_t y) {
    const yzbucket <Size> * foo = nullptr;
    for (auto x = 0u; x != NX; ++x) {
        this->index [x] = (offset) (foo [x][y].bytes - reinterpret_cast <const std::uint8_t *> (foo));
    }
}
template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <unsigned Size>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::indexer <Size> ::initU (std::size_t x) {
    const yzbucket <Size> * foo = nullptr;
    for (auto y = 0u; y != NY; ++y) {
        this->index [y] = (offset) (foo [x][y].bytes - reinterpret_cast <const std::uint8_t *> (foo));
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <unsigned Size>
std::size_t cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::indexer <Size> ::storeV (yzbucket <Size> * buckets, std::size_t y) {
    auto base = reinterpret_cast <const std::uint8_t *> (buckets);
    std::size_t sum = 0;
    for (auto x = 0u; x != NX; ++x) {
        sum += buckets [x][y].setsize (base + this->index [x]);
    }
    return sum;
}
template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <unsigned Size>
std::size_t cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::indexer <Size> ::storeU (yzbucket <Size> * buckets, std::size_t x) {
    auto base = reinterpret_cast <const std::uint8_t *> (buckets);
    std::size_t sum = 0;
    for (auto y = 0u; y != NY; ++y) {
        sum += buckets [x][y].setsize (base + this->index [y]);
    }
    return sum;
}

// solver

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <typename Callback>
std::size_t cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::solve (const std::uint8_t (&seed) [Generator::width], Callback callback) {

    // split workload into work items
    //  - 64 items for complexity 26 and 27, 128 items for complexity 28 and 29

    const auto n = this->work.size ();
    if (!this->threadpool.init (n))
        return 0;

    for (auto i = 0; i != n; ++i) {
        this->work [i].solver = this;
        this->work [i].start = i * NY / n;
        this->work [i].end = (i + 1) * NY / n;
    }

    this->generator.seed (seed);

    // actually preallocate the memory for faster performance
    //  - as I expect 32-bit build will be used only on slow/small devices which may not have 4 GB RAM,
    //    I don't do this as it would actually degrade performance by introducting extensive swapping

    if (sizeof (std::size_t) > sizeof (unsigned int)) {
        this->touch (this->buckets, sizeof (zbucket<ZBUCKETSIZE>) * NY * NX, this->cancel);
    }

    // trim

    if (!this->cancelled ()) {
        this->threadpool.begin ();
            for (auto & t : this->work) this->threadpool.dispatch (&fiber::genUnodes, &t, true);
        this->threadpool.join ();
        this->threadpool.begin ();
            for (auto & t : this->work) this->threadpool.dispatch (&fiber::genVnodes, &t, true);
        this->threadpool.join ();

        this->round = 2;
        for (; (this->round != ((Complexity > 30) ? 96u : 68u) - 2) && !this->cancelled (); this->round += 2) {
            this->threadpool.begin ();
                for (auto & t : this->work) this->threadpool.dispatch (&fiber::template trimRound <true>, &t, true);
            this->threadpool.join ();
            this->threadpool.begin ();
                for (auto & t : this->work) this->threadpool.dispatch (&fiber::template trimRound <false> , &t, true);
            this->threadpool.join ();
        }

        this->threadpool.begin ();
            for (auto & t : this->work) this->threadpool.dispatch (&fiber::template trimRename1 <true>, &t, true);
        this->threadpool.join ();
        this->threadpool.begin ();
            for (auto & t : this->work) this->threadpool.dispatch (&fiber::template trimRename1 <false>, &t, true);
        this->threadpool.join ();
    }

    // solution recovery

    if (!this->cancelled ()) {
        this->uxymap.reset ();

        std::uint32_t us [MAXPATHLEN];
        std::uint32_t vs [MAXPATHLEN];
        std::memset (this->results, ~0, (2 * NX * NYZ2) * sizeof (std::uint32_t));

        for (auto vx = 0u; (vx != NX) && !this->cancelled (); ++vx) {
            for (auto ux = 0u; (ux != NX) && !this->cancelled (); ++ux) {

                auto readbig = this->buckets [ux] [vx].words;
                auto endreadbig = readbig + this->buckets [ux] [vx].size / sizeof (std::uint32_t);

                for (; (readbig != endreadbig) && !this->cancelled (); ++readbig) {

                    auto e = *readbig;
                    auto uxyz = (ux << YZ2BITS) | (e >> YZ2BITS);
                    auto vxyz = (vx << YZ2BITS) | (e & YZ2MASK);
                    auto u0 = uxyz << 1, v0 = (vxyz << 1) | 1;

                    if (u0 != ~0) {
                        auto nu = this->path (this->results, u0, us);
                        auto nv = this->path (this->results, v0, vs);

                        if (nu != ~0 && nv != ~0) {
                            if (us [nu] == vs [nv]) {
                                auto min = nu < nv ? nu : nv;
                                for (nu -= min, nv -= min; us [nu] != vs [nv]; nu++, nv++);

                                this->length = nu + nv + 1;
                                if (this->length >= this->shortest && this->length <= this->longest) {
                                    auto ni = 0u;
                                    this->recordedge (ni++, *us, *vs);

                                    while (nu--) this->recordedge (ni++, us [(nu + 1) & ~1], us [nu | 1]);
                                    while (nv--) this->recordedge (ni++, vs [nv | 1], vs [(nv + 1) & ~1]);

                                    this->threadpool.begin ();
                                    for (auto & t : this->work) {
                                        this->threadpool.dispatch (&fiber::match, &t, true);
                                    }
                                    this->threadpool.join ();

                                    // return the solution to caller

                                    std::sort (&this->solution [0], &this->solution [this->length]);
                                    if (callback (&this->solution [0], this->length))
                                        return this->length;
                                }
                            } else
                                if (nu < nv) {
                                    while (nu--) {
                                        this->results [us [nu + 1]] = us [nu];
                                    }
                                    this->results [u0] = v0;
                                } else {
                                    while (nv--) {
                                        this->results [vs [nv + 1]] = vs [nv];
                                    }
                                    this->results [v0] = u0;
                                }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
std::uint32_t cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::path (std::uint32_t * cycle, std::uint32_t u, std::uint32_t * us) const {
    std::uint32_t u0 = u;
    std::uint32_t nu = 0;

    for (; u != ~0; u = cycle [u]) {
        if (nu < MAXPATHLEN) {
            us [nu++] = u;
        } else
            return ~0;
    }
    return nu - 1;
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::recordedge (unsigned int i, unsigned int u2, unsigned int v2) {
    auto u1 = u2 / 2;
    auto ux = u1 >> YZ2BITS;
    auto uyz = this->buckets [ux] [(u1 >> Z2BITS) & YMASK].renameu1 [u1 & Z2MASK];

    auto v1 = v2 / 2;
    auto vx = v1 >> YZ2BITS;
    auto vyz = this->buckets [(v1 >> Z2BITS) & YMASK] [vx].renamev1 [v1 & Z2MASK];

    if (COMPRESSROUND > 0) {
        uyz = this->buckets [ux] [(uyz >> Z1BITS) & YMASK].renameu [uyz & Z1MASK];
        vyz = this->buckets [(vyz >> Z1BITS) & XMASK] [vx].renamev [vyz & Z1MASK];
    }

    if (i < MAXPATHLEN) {
        this->cycleus [i] = (ux << YZBITS) | uyz;
        this->cyclevs [i] = (vx << YZBITS) | vyz;
        this->uxymap [std::size_t (this->cycleus [i] >> ZBITS)] = 1;
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::touch (void * p_, std::size_t n, volatile bool * cancel) {
    auto p = reinterpret_cast <std::uint8_t *> (p_);
    for (std::size_t i = 0; (i < n) && (cancel == nullptr || *cancel == false); i += 4096) {
        *reinterpret_cast <std::uint32_t *> (p + i) = 0;
    }
}

// solver trimming threads

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::genUnodes () {
    indexer <ZBUCKETSIZE>   destination;
    std::size_t             last [NX];

    auto edge = this->start << YZBITS;
    auto endedge = edge + NYZ;

    for (auto my = this->start; (my < this->end) && !this->solver->cancelled (); my++, endedge += NYZ) {
        destination.initV (my);
        if (NEEDSYNC) {
            for (auto x = 0u; x != NX; ++x) {
                last [x] = edge;
            }
        }
        for (; edge < endedge; edge += Generator::parallelism) {

            typename Generator::type node [Generator::parallelism];
            typename Generator::type source [Generator::parallelism];

            for (auto i = 0u; i != Generator::parallelism; ++i) {
                source [i] = 2 * (edge + i);
            }

            this->solver->generator (node, source);

            for (auto i = 0u; i != Generator::parallelism; ++i) {
                auto ux = (node [i] & EDGEMASK) >> YZBITS;
                auto zz = (offset) ((edge + i) << YZBITS) | (node [i] & YZMASK);

                if (NEEDSYNC) {
                    if (i || zz) {
                        for (; (last [ux] + NNONYZ) <= (edge + i); last [ux] += NNONYZ, destination.index [ux] += BIGSIZE0) {
                            *reinterpret_cast <std::uint32_t *> (this->solver->base + destination.index [ux]) = 0;
                        }
                        *reinterpret_cast <std::uint32_t *> (this->solver->base + destination.index [ux]) = (std::uint32_t) zz;
                        destination.index [ux] += BIGSIZE0;
                        last [ux] = edge + i;
                    }
                } else {
                    *reinterpret_cast <offset *> (this->solver->base + destination.index [ux]) = (std::uint32_t) zz;
                    destination.index [ux] += BIGSIZE0;
                }
            }
        }
        if (NEEDSYNC) {
            for (auto ux = 0u; ux != NX; ++ux) {
                for (; last [ux] < endedge - NNONYZ; last [ux] += NNONYZ) {
                    *reinterpret_cast <std::uint32_t *> (this->solver->base + destination.index [ux]) = 0;
                    destination.index [ux] += BIGSIZE0;
                }
            }
        }
        destination.storeV (this->solver->buckets, my);
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::genVnodes () {
    static const auto NONDEGBITS = std::min (BIGSLOTBITS, 2 * YZBITS) - ZBITS;
    static const auto NONDEGMASK = (1u << NONDEGBITS) - 1u;

    indexer <ZBUCKETSIZE> destination;
    indexer <TBUCKETSIZE> small;

    auto small0 = reinterpret_cast <std::uint8_t *> (&this->bucket);

    for (auto ux = this->start; (ux != this->end) && !this->solver->cancelled (); ++ux) {
        small.initU (0);
        for (auto my = 0u; my != NY; ++my) {

            auto edge = my << YZBITS;
            auto readbig = this->solver->buckets [ux] [my].bytes;
            auto endreadbig = readbig + this->solver->buckets [ux] [my].size;

            for (; readbig < endreadbig; readbig += BIGSIZE0) {
                offset e = *(offset *) readbig;
                if (BIGSIZE0 > 4) {
                    e &= BIGSLOTMASK0;
                } else
                    if (NEEDSYNC) {
                        if (!e) {
                            edge += NNONYZ;
                            continue;
                        }
                    }
                edge += ((std::uint32_t) (e >> YZBITS) - edge) & (NNONYZ - 1u);
                const auto uy = (e >> ZBITS) & YMASK;

                *(std::uint64_t *) (small0 + small.index [uy]) = ((std::uint64_t) edge << ZBITS) | (e & ZMASK);
                small.index [uy] += SMALLSIZE;
            }
        }

        small.storeU (&this->bucket, 0);
        destination.initU (ux);

        for (auto uy = 0u; uy != NY; ++uy) {
            std::memset (this->deg, 0xFF, NZ);

            auto readsmall = this->bucket [uy].bytes;
            auto endreadsmall = readsmall + this->bucket [uy].size;

            for (auto rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SMALLSIZE) {
                ++this->deg [*(std::uint16_t *) rdsmall & ZMASK];
            }

            auto zs = this->z;
            auto edges0 = this->edge;
            auto edgesE = &this->edge [NTRIMMEDZ];
            auto edges = edges0;
            auto edge = 0u;

            for (auto rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SMALLSIZE) {
                auto e = *(std::uint64_t *) rdsmall;
                edge += ((e >> ZBITS) - edge) & NONDEGMASK;
                *edges = edge;

                auto z = e & ZMASK;
                *zs = (std::uint16_t) z;

                if (this->deg [z]) {
                    ++edges;
                    ++zs;

                    if (edges >= edgesE)
                        break;
                }
            }

            auto readz = this->z;
            auto readedge = edges0;
            auto uy34 = (std::int64_t) uy << YZZBITS;

            if (Generator::parallelism > 1) {
                for (; readedge <= edges - Generator::parallelism; readedge += Generator::parallelism, readz += Generator::parallelism) {
                    typename Generator::type node [Generator::parallelism];
                    typename Generator::type source [Generator::parallelism];
                    typename std::uint64_t readzs [Generator::parallelism];

                    for (auto i = 0u; i != Generator::parallelism; ++i) { // TODO: can be vectorized like in mean_miner.hpp
                        source [i] = (2 * readedge [i]) | 1;
                        readzs [i] =(std::uint64_t) readz [i] << YZBITS;
                    }

                    this->solver->generator (node, source);

                    for (auto i = 0u; i != Generator::parallelism; ++i) { // TODO: can be vectorized like in mean_miner.hpp
                        auto vx = ((node [i] & EDGEMASK) >> YZBITS) & XMASK;
                        *reinterpret_cast <std::uint64_t *> (this->solver->base + destination.index [vx]) = uy34 | readzs [i] | (node [i] & YZMASK);

                        destination.index [vx] += BIGSIZE;
                    }
                }
            }

            for (; readedge < edges; ++readedge, ++readz) {
                auto node = this->solver->generator (2 * (*readedge) | 1);
                auto vx = ((node & EDGEMASK) >> YZBITS) & XMASK;
                auto value64 = uy34 | ((std::uint64_t) *readz << YZBITS) | (node & YZMASK);

                *reinterpret_cast <std::uint64_t *> (this->solver->base + destination.index [vx]) = value64;
                            
                // TODO: this->buckets->write <64> (destination.index [vx], value64);
                // TODO: OR this->matrix->write <64> (destination.index [vx], value64);

                destination.index [vx] += BIGSIZE;
            }
        }
        destination.storeU (this->solver->buckets, ux);
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <unsigned SRCSIZE, unsigned DSTSIZE, bool TRIMONV>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::trimEdges () {

    const auto SRCSLOTBITS = std::min (SRCSIZE * 8, 2 * YZBITS);
    const auto SRCSLOTMASK = (1uLL << SRCSLOTBITS) - 1uLL;
    const auto SRCPREFBITS = SRCSLOTBITS - YZBITS;
    const auto SRCPREFMASK = (1uLL << SRCPREFBITS) - 1uLL;
    const auto DSTSLOTBITS = std::min (DSTSIZE * 8, 2 * YZBITS);
    const auto DSTSLOTMASK = (1uLL << DSTSLOTBITS) - 1uLL;
    const auto DSTPREFBITS = DSTSLOTBITS - YZZBITS;
    const auto DSTPREFMASK = (1uLL << DSTPREFBITS) - 1uLL;

    indexer <ZBUCKETSIZE> destination;
    indexer <TBUCKETSIZE> small;

    auto small0 = reinterpret_cast <std::uint8_t *> (&this->bucket);

    for (auto vx = this->start; vx != this->end; ++vx) {
        small.initU (0);

        for (auto ux = 0u; ux != NX; ++ux) {
            auto uxyz = ux << YZBITS;

            zbucket <ZBUCKETSIZE> & zb = TRIMONV ? this->solver->buckets [ux] [vx] : this->solver->buckets [vx] [ux];

            auto readbig = zb.bytes;
            auto endreadbig = readbig + zb.size;

            for (; readbig < endreadbig; readbig += SRCSIZE) {
                auto e = *(std::uint64_t *) readbig & SRCSLOTMASK;
                uxyz += ((std::uint32_t) (e >> YZBITS) - uxyz) & SRCPREFMASK;

                auto vy = (e >> ZBITS) & YMASK;

                *(std::uint64_t *) (small0 + small.index [vy]) = ((std::uint64_t) uxyz << ZBITS) | (e & ZMASK);
                uxyz &= ~ZMASK;
                small.index [vy] += DSTSIZE;
            }
        }

        small.storeU (&this->bucket, 0);
        if (TRIMONV) {
            destination.initV (vx);
        } else {
            destination.initU (vx);
        }

        for (auto vy = 0u; vy != NY; ++vy) {
            auto vy34 = (std::uint64_t) vy << YZZBITS;
                        
            std::memset (this->deg, 0xff, NZ);

            auto readsmall = this->bucket [vy].bytes;
            auto endreadsmall = readsmall + this->bucket [vy].size;

            for (auto rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SMALLSIZE) {
                ++this->deg [*(std::uint32_t *) rdsmall & ZMASK];
            }

            auto ux = 0u;
            for (auto rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += DSTSIZE) {

                auto e = *(std::uint64_t *) rdsmall & DSTSLOTMASK;
                ux += ((std::uint32_t) (e >> YZZBITS) - ux) & DSTPREFMASK;

                if (ux < NX) {
                    *(std::uint64_t *) (this->solver->base + destination.index [ux]) = vy34 | ((e & ZMASK) << YZBITS) | ((e >> ZBITS) & YZMASK);

                    if (this->deg [e & ZMASK]) {
                        destination.index [ux] += DSTSIZE;
                    }
                }
            }
        }
        if (TRIMONV) {
            destination.storeV (this->solver->buckets, vx);
        } else {
            destination.storeU (this->solver->buckets, vx);
        }
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <unsigned SRCSIZE, unsigned DSTSIZE, bool TRIMONV>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::trimRename () {

    const auto SRCSLOTBITS = std::min (SRCSIZE * 8, (TRIMONV ? YZBITS : YZ1BITS) + YZBITS);
    const auto SRCSLOTMASK = (1uLL << SRCSLOTBITS) - 1uLL;
    const auto SRCPREFBITS = SRCSLOTBITS - YZBITS;
    const auto SRCPREFMASK = (1uLL << SRCPREFBITS) - 1uLL;
    const auto SRCPREFBITS2 = SRCSLOTBITS - YZZBITS;
    const auto SRCPREFMASK2 = (1uLL << SRCPREFBITS2) - 1uLL;

    indexer <ZBUCKETSIZE> destination;
    indexer <TBUCKETSIZE> small;

    auto maxnnid = 0u;
    auto small0 = reinterpret_cast <std::uint8_t *> (&this->bucket);

    for (auto vx = this->start; vx != this->end; ++vx) {
        small.initU (0);

        for (auto ux = 0u; ux != NX; ++ux) {
            auto uyz = 0u;

            zbucket <ZBUCKETSIZE> & zb = TRIMONV ? this->solver->buckets [ux][vx] : this->solver->buckets [vx][ux];

            auto readbig = zb.bytes;
            auto endreadbig = readbig + zb.size;

            for (; readbig < endreadbig; readbig += SRCSIZE) {

                auto e = *(std::uint64_t *) readbig & SRCSLOTMASK;
                if (TRIMONV) {
                    uyz += ((std::uint32_t) (e >> YZBITS) - uyz) & SRCPREFMASK;
                } else {
                    uyz = (std::uint32_t) (e >> YZBITS);
                }

                auto vy = (e >> ZBITS) & YMASK;

                *(std::uint64_t *) (small0 + small.index [vy]) = ((std::uint64_t) (ux << (TRIMONV ? YZBITS : YZ1BITS) | uyz) << ZBITS) | (e & ZMASK);

                if (TRIMONV) {
                    uyz &= ~ZMASK;
                }
                small.index [vy] += SRCSIZE;
            }
        }


        small.storeU (&this->bucket, 0);
        if (TRIMONV) {
            destination.initV (vx);
        } else {
            destination.initU (vx);
        }

        std::uint32_t newnodeid = 0u;
        std::uint32_t * renames = nullptr;
        if (TRIMONV) {
            renames = this->solver->buckets [0][vx].renamev;
        } else {
            renames = this->solver->buckets [vx][0].renameu;
        }
        std::uint32_t * endrenames = renames + NZ1;

        auto degs = (std::uint16_t *) this->deg;

        for (auto vy = 0u; vy != NY; ++vy) {
            std::memset (degs, 0xff, 2 * NZ);

            auto readsmall = this->bucket [vy].bytes;
            auto endreadsmall = readsmall + this->bucket [vy].size;

            for (auto rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SRCSIZE) {
                ++degs [*(std::uint32_t *) rdsmall & ZMASK];
            }

            auto ux = 0u;
            auto nrenames = 0u;

            for (auto rdsmall = readsmall; rdsmall < endreadsmall; rdsmall += SRCSIZE) {

                auto e = *(std::uint64_t *) rdsmall & SRCSLOTMASK;
                if (TRIMONV) {
                    ux += ((std::uint32_t) (e >> YZZBITS) - ux) & SRCPREFMASK2;
                } else {
                    ux = (std::uint32_t) (e >> YZZ1BITS);
                }

                auto vz = e & ZMASK;
                auto vdeg = degs [vz];

                if (vdeg) {
                    if (vdeg < 32) {
                        vdeg = 32 + nrenames++;
                        degs [vz] = (std::uint8_t) vdeg;

                        *renames++ = (std::uint32_t) (vy << ZBITS | vz);
                        if (renames == endrenames) {
                            endrenames += (TRIMONV ? sizeof (yzbucket <ZBUCKETSIZE>) : sizeof (zbucket <ZBUCKETSIZE>)) / sizeof (std::uint32_t);
                            renames = endrenames - NZ1;
                        }
                    }

                    if (TRIMONV) {
                        *(std::uint64_t *) (this->solver->base + destination.index [ux]) = ((std::uint64_t) (newnodeid + vdeg - 32) << YZBITS) | ((e >> ZBITS) & YZMASK);
                    } else {
                        *(std::uint32_t *) (this->solver->base + destination.index [ux]) = ((newnodeid + vdeg - 32) << YZ1BITS) | ((e >> ZBITS) & YZ1MASK);
                    }
                    destination.index [ux] += DSTSIZE;
                }
            }
            newnodeid += nrenames;
        }
        if (maxnnid < newnodeid) {
            maxnnid = newnodeid;
        }
        if (TRIMONV) {
            destination.storeV (this->solver->buckets, vx);
        } else {
            destination.storeU (this->solver->buckets, vx);
        }
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <bool TRIMONV>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::trimEdges1 () {

    indexer <ZBUCKETSIZE> destination;

    for (auto vx = this->start; vx != this->end; ++vx) {
        if (TRIMONV) {
            destination.initV (vx);
        } else {
            destination.initU (vx);
        }

        std::memset (this->deg, 0xFF, NYZ1);

        for (auto ux = 0u; ux != NX; ++ux) {
            zbucket <ZBUCKETSIZE> & zb = TRIMONV ? this->solver->buckets [ux][vx] : this->solver->buckets [vx][ux];

            auto readbig = zb.words;
            auto endreadbig = readbig + zb.size / sizeof (std::uint32_t);

            for (; readbig != endreadbig; ++readbig) {
                ++this->deg [*readbig & YZ1MASK];
            }
        }
        for (auto ux = 0u; ux != NX; ++ux) {
            zbucket <ZBUCKETSIZE> & zb = TRIMONV ? this->solver->buckets [ux][vx] : this->solver->buckets [vx][ux];

            auto readbig = zb.words;
            auto endreadbig = readbig + zb.size / sizeof (std::uint32_t);

            for (; readbig != endreadbig; ++readbig) {
                auto e = *readbig;
                auto vyz = e & YZ1MASK;

                *(std::uint32_t *) (this->solver->base + destination.index [ux]) = (vyz << YZ1BITS) | (e >> YZ1BITS);

                if (this->deg [vyz]) {
                    destination.index [ux] += sizeof (std::uint32_t);
                }
            }
        }
        if (TRIMONV) {
            destination.storeV (this->solver->buckets, vx);
        } else {
            destination.storeU (this->solver->buckets, vx);
        }
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <bool TRIMONV>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::trimRename1 () {

    indexer <ZBUCKETSIZE> destination;

    auto maxnnid = 0u;
    auto degs = (std::uint16_t *) this->deg;

    for (auto vx = this->start; (vx != this->end) && !this->solver->cancelled (); ++vx) {
        if (TRIMONV) {
            destination.initV (vx);
        } else {
            destination.initU (vx);
        }

        std::memset (degs, 0xFF, 2 * NYZ1);

        for (auto ux = 0u; ux != NX; ++ux) {
            zbucket <ZBUCKETSIZE> & zb = TRIMONV ? this->solver->buckets [ux][vx] : this->solver->buckets [vx][ux];

            auto readbig = zb.words;
            auto endreadbig = readbig + zb.size / sizeof (std::uint32_t);

            for (; readbig != endreadbig; ++readbig) {
                ++degs [*readbig & YZ1MASK];
            }
        }

        std::uint32_t newnodeid = 0u;
        std::uint32_t * renames = nullptr;
        if (TRIMONV) {
            renames = this->solver->buckets [0][vx].renamev1;
        } else {
            renames = this->solver->buckets [vx][0].renameu1;
        }
        std::uint32_t * endrenames = renames + NZ2;

        for (auto ux = 0u; ux != NX; ++ux) {
            zbucket <ZBUCKETSIZE> & zb = TRIMONV ? this->solver->buckets [ux][vx] : this->solver->buckets [vx][ux];

            auto readbig = zb.words;
            auto endreadbig = readbig + zb.size / sizeof (std::uint32_t);

            for (; readbig != endreadbig; ++readbig) {
                auto e = *readbig;
                auto vyz = e & YZ1MASK;
                auto vdeg = degs [vyz];
                if (vdeg) {
                    if (vdeg < 32) {
                        vdeg = 32 + newnodeid++;
                        degs [vyz] = vdeg;
                        *renames++ = vyz;

                        if (renames == endrenames) {
                            endrenames += (TRIMONV ? sizeof (yzbucket <ZBUCKETSIZE>) : sizeof (zbucket <ZBUCKETSIZE>)) / sizeof (std::uint32_t);
                            renames = endrenames - NZ2;
                        }
                    }

                    *(std::uint32_t *) (this->solver->base + destination.index [ux]) = ((vdeg - 32) << (TRIMONV ? YZ1BITS : YZ2BITS)) | (e >> YZ1BITS);
                    destination.index [ux] += sizeof (std::uint32_t);
                }
            }
        }

        if (maxnnid < newnodeid) {
            maxnnid = newnodeid;
        }
        if (TRIMONV) {
            destination.storeV (this->solver->buckets, vx);
        } else {
            destination.storeU (this->solver->buckets, vx);
        }
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
template <bool TRIMONV>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::trimRound () {

    auto c = this->solver->round;
    auto e = this->solver->round + !TRIMONV;

    if (c < COMPRESSROUND) {
        if (e < EXPANDROUND) {
            this->trimEdges <BIGSIZE, BIGSIZE, TRIMONV> ();
        } else
        if (e == EXPANDROUND) {
            this->trimEdges <BIGSIZE, BIGGERSIZE, TRIMONV> ();
        } else {
            this->trimEdges <BIGGERSIZE, BIGGERSIZE, TRIMONV> ();
        }
    } else
    if (c == COMPRESSROUND) {
        if (TRIMONV) {
            this->trimRename <BIGGERSIZE, BIGGERSIZE, TRIMONV> ();
        } else {
            this->trimRename <BIGGERSIZE, sizeof (std::uint32_t), TRIMONV> ();
        }
    } else {
        this->trimEdges1 <TRIMONV> ();
    }
}

template <unsigned Complexity, typename Generator, template <typename> class ThreadPoolControl>
void cuckoo::solver <Complexity, Generator, ThreadPoolControl> ::fiber::match () {
    auto edge = this->start << YZBITS;
    auto endedge = edge + NYZ;

    for (auto my = this->start; (my != this->end) && !this->solver->cancelled (); ++my, endedge += NYZ) {
        for (; edge < endedge; edge += Generator::parallelism) {
                        
            typename Generator::type node [Generator::parallelism];
            typename Generator::type source [Generator::parallelism];

            for (auto i = 0u; i != Generator::parallelism; ++i) {
                source [i] = 2 * (edge + i);
            }

            this->solver->generator (node, source);

            for (auto i = 0u; i != Generator::parallelism; ++i) {
                node [i] &= EDGEMASK;

                if (this->solver->uxymap [std::size_t (node [i] >> ZBITS)]) {
                    for (auto j = 0u; j != this->solver->length; ++j) {
                        if (this->solver->cycleus [j] == node [i]) {
                            if (this->solver->cyclevs [j] == (this->solver->generator (2 * (edge + i) + 1) & EDGEMASK)) {
                                this->solver->solution [j] = edge + i;
                            }
                        }
                    }
                }
            }
        }
    }
}

// verify

template <typename Generator>
bool cuckoo::verify (unsigned complexity,
                     const std::uint8_t (&seed) [Generator::width],
                     const std::uintmax_t * cycle, std::size_t length) {
    Generator generator;
    generator.seed (seed);

    std::uintmax_t xor0 = 0;
    std::uintmax_t xor1 = 0;
    std::vector <std::uintmax_t> uvs (2 * length);

    for (std::size_t n = 0; n != length; ++n) {
        if (cycle [n] >= (1uLL << complexity)) return false; // too large node
        if (n && cycle [n] <= cycle [n - 1]) return false; // not sorted

        xor0 ^= uvs [2 * n + 0] = ((1uLL << complexity) - 1u) & generator (2 * cycle [n] + 0);
        xor1 ^= uvs [2 * n + 1] = ((1uLL << complexity) - 1u) & generator (2 * cycle [n] + 1);
    }
    if (xor0 | xor1)
        return false;

    auto n = 0u;
    auto i = 0u;
    auto j = 0u;
    do {
        for (auto k = j = i; (k = (k + 2) % (2 * length)) != i; ) {
            if (uvs [k] == uvs [i]) {
                if (j != i)
                    return false;

                j = k;
            }
        }
        if (j == i)
            return false;

        i = j ^ 1;
        ++n;
    } while (i != 0);

    return n == length;
}

#endif
