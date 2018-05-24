#ifndef RADDI_DATABASE_ROW_H
#define RADDI_DATABASE_ROW_H

#include "raddi_database.h"
#include "raddi_consensus.h"
#include "raddi_identity.h"
#include "raddi_channel.h"

// row
//  - in-memory and on-disk representation of stored index key of an data entry 
//  - signatures and text (content) is contained in separate 'content' file
//
struct raddi::db::row {
    eid id;
    eid parent;

    // data
    //  - address of signature and content data in 'text' file
    //  - length does NOT include signature and proof bytes
    //
    struct __attribute__ ((ms_struct)) {
        std::uint64_t offset : 48;
        std::uint64_t length : 16;
    } data;

    // top
    //  - parallel index for fast lookup of entries that belong to channel or thread
    //
    root top_;

    // type
    //  - differentiation index based on the content to improve search performance
    //  - TODO: this will be classification bit-field
    //
    std::uint64_t type;

public:

    // comparison operator
    //  - entry ID is unique entry identifier in the network
    //
    friend inline bool operator < (const row & a, const row & b) { return a.id < b.id; }
    
    // operator raddi::entry
    //  - creates entry id/parent pair according to this row
    //
    explicit operator raddi::entry () const {
        return { this->id, this->parent };
    }

    // top
    //  - returns appropriate 'root'
    //  - function so that other 'Xrow' classes can generate them on the fly
    //
    inline const root & top () const {
        return this->top_;
    }

    // classify
    //  - parses entry and initializes members other than 'data'
    //
    inline bool classify (const void * data, std::size_t size, const raddi::db::root & top) {
        auto entry = reinterpret_cast <const raddi::entry *> (data);

        this->id = entry->id;
        this->parent = entry->parent;
        this->top_ = top;
        this->type = 0; // TODO: parse entry content and classify...
                        // 'this->type->classify (entry->content (), size - sizeof (raddi::entry));

        return true;
    }
};

// trow
//  - specialized 'row' for base thread data entry 
//
struct raddi::db::trow {
    eid id;
    eid parent;

    // data
    //  - address of signature and content data in 'text' file
    //  - length does NOT include signature and proof bytes
    //
    struct __attribute__ ((ms_struct)) {
        std::uint64_t offset : 48;
        std::uint64_t length : 16;
    } data;

public:

    // comparison operator
    //  - entry ID is unique entry identifier in the network
    //
    friend inline bool operator < (const trow & a, const trow & b) { return a.id < b.id; }

    // operator raddi::entry
    //  - creates entry id/parent pair according to this identity row
    //
    explicit operator raddi::entry () const {
        return { this->id, this->parent };
    }

    // top
    //  - generates 'root' for this thread entry
    //
    inline root top () const {
        return { this->parent, this->id };
    }

    // classify
    //  - parses entry and initializes members other than 'data'
    //
    inline bool classify (const void * data, std::size_t size, const raddi::db::root & top) {
        this->id = reinterpret_cast <const raddi::entry *> (data)->id;
        this->parent = reinterpret_cast <const raddi::entry *> (data)->parent;
        return true;
    }
};

// crow
//  - specialized 'row' for channel announcement entry 
//
struct raddi::db::crow {
    eid id;

    // length_bits
    //  - 
    //  - NOTE: database.settings.maximum_shard_size MUST be smaller than 
    //          (1 << (32-length_bits)) / ((1 << length_bits) - 1 + sizeof (entry))
    //           - TODO: check when loading from command line parameters
    //
    static constexpr const auto length_bits = 6;

    // data
    //  - address of signature and content data in 'text' file
    //  - length does NOT include signature and proof bytes
    //
    struct __attribute__ ((ms_struct)) {
        std::uint32_t offset : 32 - length_bits;
        std::uint32_t length : length_bits;
    } data;

    static_assert ((1 << length_bits) > (sizeof (raddi::channel) - sizeof (raddi::entry) + raddi::consensus::max_channel_name_size));

public:

    // comparison operator
    //  - entry ID is unique entry identifier in the network
    //
    friend inline bool operator < (const crow & a, const crow & b) { return a.id < b.id; }

    // operator raddi::entry
    //  - creates entry id/parent pair according to this identity row
    //
    explicit operator raddi::entry () const {
        return { this->id, this->id };
    }

    // top
    //  - generates appropriate 'root'
    //
    inline root top () const {
        return { this->id, this->id };
    }

    // classify
    //  - parses entry and initializes members other than 'data'
    //
    inline bool classify (const void * entry, std::size_t size, const raddi::db::root & top) {
        this->id = reinterpret_cast <const raddi::channel *> (entry)->id;

        // allow insertion only if the identity entry will fit
        return size < sizeof (raddi::entry) + (1 << length_bits);
    }
};

// irow
//  - specialized 'row' for identity announcement entry 
//  - we bother mainly because of this; identities will need either to stay in memory
//    or load fast and this way we load 4 times less data than if just using 'row'
//
struct raddi::db::irow {
    iid id;

    // length_bits
    //  - 
    //  - NOTE: database.settings.maximum_shard_size MUST be smaller than 
    //          (1 << (32-length_bits)) / ((1 << length_bits) - 1 + sizeof (entry))
    //           - TODO: check when loading from command line parameters
    //
    static constexpr const auto length_bits = 7;

    // data
    //  - address of signature and content data in 'text' file
    //  - length does NOT include signature and proof bytes
    //
    struct __attribute__ ((ms_struct)) {
        std::uint32_t offset : 32 - length_bits;
        std::uint32_t length : length_bits;
    } data;

    static_assert ((1 << length_bits) > (sizeof (raddi::identity) - sizeof (raddi::entry) + raddi::consensus::max_identity_name_size));

public:

    // comparison operator
    //  - identity ID will suffice in table of identities
    //
    friend inline bool operator < (const irow & a, const irow & b) { return a.id < b.id; }

    // operator raddi::entry
    //  - creates entry id/parent pair according to this identity row
    //
    explicit operator raddi::entry () const {
        return { this->id, this->id };
    }

    // top
    //  - generates appropriate 'root'
    //
    inline root top () const {
        return { this->id, this->id };
    }

    // classify
    //  - parses entry and initializes 'id'
    //
    inline bool classify (const void * entry, std::size_t size, const raddi::db::root & top) {
        this->id = reinterpret_cast <const raddi::identity *> (entry)->id.identity;

        // allow insertion only if the identity entry will fit
        return size < sizeof (raddi::entry) + (1 << length_bits);
    }
};

// read
//  - for 'get' function specifies what parts of the entry should be provided
//  - internal parameter, actually a bitmask (would be nice to benefit from it though)
//
enum class raddi::db::read : unsigned int {
    nothing = 0,
    identification = 1,
    verification = 2,
    identification_and_verification = 3,
    content = 4,
    identification_and_content = 5,
    verification_and_content = 6,
    everything = 7
};

#endif
