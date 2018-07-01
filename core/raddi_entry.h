#ifndef RADDI_ENTRY_H
#define RADDI_ENTRY_H

#include "raddi_eid.h"
#include "raddi_proof.h"
#include "raddi_protocol.h"
#include "raddi_consensus.h"

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

    public:

        // validate
        //  - performs basic validation of protocol frame content, 'size' must be exact
        //
        static bool validate (const void * entry, std::size_t size);

        // proof
        //  - finds proof-of-work at the end of the entry, returns nullptr if missing
        //     - entry and 'size' must be validated first!
        //  - 'size' specifies number of bytes in the received entry
        //  - 'proof_size', optional, receives size of the proof-of-work in bytes
        //  - NOTES:
        //     - proof of work follows 'content' after (and including) a NUL byte
        //     - channel/identity announcements require increased complexity
        //
        const proof * proof (std::size_t size, std::size_t * proof_size = nullptr) const;

        // content
        //  - return pointer to first byte after entry header (entry content)
        //
        inline       std::uint8_t * content ()       { return reinterpret_cast <      std::uint8_t *> (this + 1); };
        inline const std::uint8_t * content () const { return reinterpret_cast <const std::uint8_t *> (this + 1); };

        // verify
        //  - verifies that entry and it's parent is signed by private-key matching public-key 'public_key'
        //  - 'size' specifies number of bytes of transported entry: raddi::entry header + data
        //  - provided entry MUST be validated first!!! or the call may crash
        //
        bool verify (std::size_t size, const entry * parent, std::size_t parent_size,
                     const std::uint8_t (&public_key) [crypto_sign_ed25519_PUBLICKEYBYTES]) const;

        // sign
        //  - proves and signs entry (of 'size' bytes) and parent (of 'parent_size' bytes) with provided 'private_key'
        //     - for identity announcement the 'parent' is null
        //     - proof requiremens are default if omitted (rq)
        //  - 'this->signature' is ignored on input, on output set to valid data
        //     - proof is appended to content and size of additional data (the proof) is returned
        //     - NOTE: maximum size of appended data is up to 'raddi::proof::max_size' but to allocate 
        //             'raddi::protocol::max_payload' bytes for a whole entry is probably a best idea
        //  - 'size' specifies number of bytes of transported entry, i.e. this entry header + data
        //  - 'cancel' - optional, if it points to existing bool, setting that bool terminates the operation
        //  - returns - on success number of bytes of additional data (the proof) appended to 'this'
        //            - 0 if failed to sign or find proof-of-work
        //            - throws std::bad_alloc if there is not enough memory to find proof-of-work
        //
        std::size_t sign (std::size_t size, const entry * parent, std::size_t parent_size,
                          const std::uint8_t (&private_key) [crypto_sign_ed25519_SECRETKEYBYTES],
                          volatile bool * cancel = nullptr);
        std::size_t sign (std::size_t size, const entry * parent, std::size_t parent_size,
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

        // default_requirements (minimal)
        //  - constructs requirements according to whether the entry is announcement or not
        //  - we have 4 complexity levels, where higher 2 are really unusable for low-end devices
        //    so the decision here is easier than I anticipated
        //
        inline proof::requirements default_requirements () const {
            if (this->is_announcement ()) {
                return {
                    consensus::min_announcement_pow_complexity,
                    consensus::min_announcement_pow_time
                };
            } else {
                return {
                    consensus::min_entry_pow_complexity,
                    consensus::min_entry_pow_time
                };
            }
        }

        // max_content_size
        //  - maximum size of content following the entry header
        //     - minimal size of proof is not substracted
        //  - content size is determined by protocol frame size information
        //    (basically UINT16_MAX minus AES signature and entry header size)
        // 
        static constexpr std::size_t max_content_size = raddi::protocol::max_payload
                                                      - sizeof raddi::entry::id
                                                      - sizeof raddi::entry::parent
                                                      - sizeof raddi::entry::signature;
    
    private:

        // prehash
        //  - begins hash phase of entry signature creation/validation
        //  - hashes 'this->id', 'this->parent', content following entry header and 'parent' data (if provided)
        //
        crypto_sign_ed25519ph_state prehash (std::size_t size, const entry * parent, std::size_t parent_size) const;
    };

    // comparison operators
    //  - entry ID is unique entry identitifer in the network

    inline bool operator == (const entry & a, const entry & b) { return a.id == b.id; }
    inline bool operator != (const entry & a, const entry & b) { return a.id != b.id; }
    inline bool operator <  (const entry & a, const entry & b) { return a.id <  b.id; }
    inline bool operator <= (const entry & a, const entry & b) { return !(b < a); }
    inline bool operator >  (const entry & a, const entry & b) { return  (b < a); }
    inline bool operator >= (const entry & a, const entry & b) { return !(a < b); }
}

#endif
