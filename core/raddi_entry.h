#ifndef RADDI_ENTRY_H
#define RADDI_ENTRY_H

#include "raddi_eid.h"
#include "raddi_proof.h"
#include "raddi_protocol.h"

namespace raddi {

    // entry header
    //  - on-wire and in-memory data entry header format
    //
    struct entry {

        // identification
        //  - location of the entry in the entries tree (channels/threads/comments...)
        //
        raddi::eid id;
        raddi::eid parent;

        // signature
        //  - verification signature of complete entry (except the 'signature' bytes, of course)
        //  - NOTE: (signature[63] & 0xE0) == 0, we could reuse the 3 bits :)
        //
        std::uint8_t signature [crypto_sign_ed25519_BYTES];

        // proof
        //  - verification proof-of-work that validates the entry
        //  - reason: attacker will need one machine for each spamming account
        //  - channel/identity announcements require increased complexity
        //
        union {
            raddi::proof proof;
            // TODO: raddi::stream stream; // (file, video, music, ...) content that follows initial entry
        };

        // content
        //  - return pointer to first byte after entry header (entry content)
        //
        inline void * content () { return this + 1; };
        inline const void * content () const { return this + 1; };

        // validate
        //  - performs basic validation of protocol frame content, 'size' must be exact
        //
        static bool validate (const void * entry, std::size_t size);

        // verify
        //  - verifies that entry and it's parent is signed by private-key matching public-key 'public_key'
        //  - 'size' specifies number of bytes of transported entry: raddi::entry header + data
        //  - provided entry MUST be validated first!
        //
        bool verify (std::size_t size, const entry * parent, std::size_t parent_size,
                     const std::uint8_t (&public_key) [crypto_sign_ed25519_PUBLICKEYBYTES]) const;

        // sign
        //  - proves and signs entry (of 'size' bytes) and parent (of 'parent_size' bytes) with provided 'private_key'
        //     - for identity announcement the 'parent' is null
        //     - proof requiremens are default if omitted (rq)
        //  - 'this->signature' and 'this->proof' are ignored on input, on output set to valid data
        //  - 'size' specifies number of bytes of transported entry, i.e. this entry header + data
        //  - 'cancel' - optional, if it points to existing bool, setting that bool terminates the operation
        //  - needs large amount of memory and throws std::bad_alloc
        //
        bool sign (std::size_t size, const entry * parent, std::size_t parent_size,
                   const std::uint8_t (&private_key) [crypto_sign_ed25519_SECRETKEYBYTES],
                   volatile bool * cancel = nullptr);
        bool sign (std::size_t size, const entry * parent, std::size_t parent_size,
                   const std::uint8_t (&private_key) [crypto_sign_ed25519_SECRETKEYBYTES],
                   proof::requirements rq, volatile bool * cancel = nullptr);

        // announcement_type
        //  - some entries announce new channel/identity creation
        //  - note that 'identity' is also a channel, i.e. target for personal messages
        //
        enum announcement_type {
            not_an_announcement = 0,
            new_channel_announcement = 1,
            new_identity_announcement = 2,
        };

        // is_announcement
        //  - determines if this entry is new user or new channel announcement
        //
               announcement_type is_announcement () const;
        static announcement_type is_announcement (const raddi::eid & id, const raddi::eid & parent);

        // default_requirements
        //  - according to eid::type and announcement_type estimates default proof requirements
        //
        proof::requirements default_requirements () const;

        // max_content_size
        //  - maximum size of content following the entry header
        //  - content size is determined by protocol frame size information
        //    (basically UINT16_MAX minus AES signature and entry header size)
        // 
        static constexpr std::size_t max_content_size = raddi::protocol::max_payload
                                                      - sizeof raddi::entry::id
                                                      - sizeof raddi::entry::parent
                                                      - sizeof raddi::entry::signature
                                                      - sizeof raddi::entry::proof;
    
    private:

        // prehash
        //  - begins hash phase of entry signature creation/validation
        //  - hashes 'this->id', 'this->parent', content following entry header and 'parent' data (when required)
        //
        crypto_sign_ed25519ph_state prehash (std::size_t size, const entry * parent, std::size_t parent_size) const;
    };

    // comparison operators
    //  - entry ID is unique entry identitifer in the network

    inline bool operator == (const entry & a, const entry & b) {
        return a.id == b.id;
    }
    inline bool operator != (const entry & a, const entry & b) {
        return a.id != b.id;
    }
    inline bool operator <  (const entry & a, const entry & b) {
        return a.id < b.id;
    }
    inline bool operator <= (const entry & a, const entry & b) {
        return !(b < a);
    }
    inline bool operator >  (const entry & a, const entry & b) {
        return  (b < a);
    };
    inline bool operator >= (const entry & a, const entry & b) {
        return !(a < b);
    };
}

#endif
