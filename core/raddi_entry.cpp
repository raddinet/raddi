#include "raddi_entry.h"
#include "raddi_channel.h"
#include "raddi_identity.h"
#include "raddi_consensus.h"

#include "../common/log.h"

#include <windows.h>
#include <cstring>
#include <vector>

// NOTE: validate/verify strings are in raddi_database.rc, "DATABASE | DATA | 0x1?" rows

bool raddi::entry::validate (const void * header, std::size_t length) {
    if (length < sizeof (entry))
        return false;

    auto e = static_cast <const entry *> (header);
    auto now = raddi::now ();

    if (raddi::older (e->id.timestamp, e->parent.timestamp)) {
        return raddi::log::data (raddi::component::database, 0x10, e->id, e->id.timestamp, e->parent.timestamp);
    }
    if (raddi::older (e->id.timestamp, e->id.identity.timestamp)) {
        return raddi::log::data (raddi::component::database, 0x11, e->id, e->id.timestamp, e->id.identity.timestamp);
    }
    if (raddi::older (e->parent.timestamp, e->parent.identity.timestamp)) {
        return raddi::log::data (raddi::component::database, 0x12, e->id, e->parent.timestamp, e->parent.identity.timestamp);
    }
    if (raddi::older (e->id.timestamp, now - 0x40000000u)) {
        return raddi::log::data (raddi::component::database, 0x13, e->id, e->id.timestamp, now - 0x40000000u, 0x40000000u / (60 * 60 * 24 * 365));
    }
    if (raddi::older (now + raddi::consensus::max_entry_skew_allowed, e->id.timestamp)) {
        return raddi::log::data (raddi::component::database, 0x14, e->id, now + raddi::consensus::max_entry_skew_allowed,
                                 e->id.timestamp, raddi::consensus::max_entry_skew_allowed);
    }

    if (length < sizeof (entry) + proof::min_size) {
        return raddi::log::data (raddi::component::database, 0x1B, e->id, length);
    }

    std::size_t proof_size;
    if (e->proof (length, &proof_size) == nullptr) {
        return raddi::log::data (raddi::component::database, 0x1B, e->id, length);
    }

    auto content_size = length - sizeof (raddi::entry) - proof_size;
    switch (e->is_announcement ()) {
        case new_identity_announcement:
            if (length < sizeof (raddi::identity)) {
                return raddi::log::data (raddi::component::database, 0x18, e->id, length, sizeof (raddi::identity));
            }

            content_size -= sizeof (raddi::identity) - sizeof (raddi::entry);
            if (content_size > raddi::consensus::max_identity_name_size) {
                return raddi::log::data (raddi::component::database, 0x1C, e->id, content_size, raddi::consensus::max_identity_name_size);
            }
            return true;

        case new_channel_announcement:
            if (length < sizeof (raddi::channel)) {
                return raddi::log::data (raddi::component::database, 0x19, e->id, length, sizeof (raddi::channel));
            }

            content_size -= sizeof (raddi::channel) - sizeof (raddi::entry);
            if (content_size > raddi::consensus::max_channel_name_size) {
                return raddi::log::data (raddi::component::database, 0x1C, e->id, content_size, raddi::consensus::max_channel_name_size);
            }
            return true;

        default:
        case not_an_announcement:
            return content_size > 0
                || raddi::log::data (raddi::component::database, 0x1A, e->id);
    }
}

const raddi::proof * raddi::entry::proof (std::size_t size, std::size_t * proof_size_result) const {
    auto content_size = size - sizeof (entry);
    auto p = reinterpret_cast <const raddi::proof *> (this->content () + content_size - 1); // last byte

    if (auto proof_size = p->validate (content_size)) {
        if (this->content () [content_size - proof_size] == 0x00) { // verify NUL byte terminating the entry
            
            if (proof_size_result) {
                *proof_size_result = proof_size;
            }
            return p;
        }
    }
    return nullptr;
}

crypto_sign_ed25519ph_state raddi::entry::prehash (std::size_t size) const {
    static_assert (sizeof raddi::protocol::magic == 8);
    static_assert (sizeof raddi::timestamp_base == 8);
    static_assert (sizeof (raddi::eid) == 12);
    static_assert (sizeof raddi::protocol::magic + sizeof raddi::timestamp_base + sizeof this->id + sizeof this->parent < 128u);

    crypto_sign_ed25519ph_state state;

    // common initialization of Ed25519ph state
    //  - manually inlined pre-initialization to prevent multiple copies and unnecessary zeroing when calling the API
    //  - the reason is that this might be quite hot function under high traffic
    //
    state.hs.state [0] = 0x6a09e667f3bcc908uLL;
    state.hs.state [1] = 0xbb67ae8584caa73buLL;
    state.hs.state [2] = 0x3c6ef372fe94f82buLL;
    state.hs.state [3] = 0xa54ff53a5f1d36f1uLL;
    state.hs.state [4] = 0x510e527fade682d1uLL;
    state.hs.state [5] = 0x9b05688c2b3e6c1fuLL;
    state.hs.state [6] = 0x1f83d9abfb41bd6buLL;
    state.hs.state [7] = 0x5be0cd19137e2179uLL;
    state.hs.count [0] = 0;
    state.hs.count [1] = (sizeof raddi::protocol::magic + sizeof raddi::timestamp_base + sizeof this->id + sizeof this->parent) << 3;

    std::memcpy (&state.hs.buf [0], raddi::protocol::magic, sizeof raddi::protocol::magic);
    std::memcpy (&state.hs.buf [8], &raddi::timestamp_base, sizeof raddi::timestamp_base);
    std::memcpy (&state.hs.buf [16], &this->id, sizeof this->id + sizeof this->parent);

    // the library takes over here

    crypto_sign_ed25519ph_update (&state, reinterpret_cast <const unsigned char *> (this->content ()), size - sizeof (entry));
    return state;
}

bool raddi::entry::verify (std::size_t size,
                           const std::uint8_t (&public_key) [crypto_sign_ed25519_PUBLICKEYBYTES]) const {
    std::size_t proof_size;
    
    auto proof = this->proof (size, &proof_size);
    auto imprint = this->prehash (size - proof_size);

    if (proof->verify (imprint.hs)) {
        crypto_sign_ed25519ph_update (&imprint, proof->data (), proof_size);
        return crypto_sign_ed25519ph_final_verify (&imprint, const_cast <std::uint8_t *> (this->signature), public_key) == 0
            || raddi::log::data (raddi::component::database, 0x1E, this->id, size);
    } else
        return raddi::log::data (raddi::component::database, 0x1F, this->id, size);
}

std::size_t raddi::entry::sign (std::size_t size,
                                const std::uint8_t (&private_key) [crypto_sign_ed25519_SECRETKEYBYTES],
                                proof::requirements rq, volatile bool * cancel) {
    if (size >= sizeof (entry)) {

        auto imprint = this->prehash (size);
        auto proof_ptr = this->content () + size - sizeof (entry);
        
        raddi::proof::options options;
        options.requirements = rq;
        options.parameters.cancel = cancel;

        if (auto proof_size = raddi::proof::generate (imprint.hs,
                                                      proof_ptr, entry::max_content_size - size,
                                                      options)) {

            crypto_sign_ed25519ph_update (&imprint, proof_ptr, proof_size);
            if (crypto_sign_ed25519ph_final_create (&imprint, this->signature, nullptr, private_key) == 0) {
                return proof_size;
            }
        }
    }
    return 0;
}

std::size_t raddi::entry::sign (std::size_t size,
                                const std::uint8_t (&private_key) [crypto_sign_ed25519_SECRETKEYBYTES],
                                volatile bool * cancel) {
    return this->sign (size, private_key,
                       this->default_requirements (), cancel);
}

raddi::entry::announcement_type raddi::entry::is_announcement () const {
    return entry::is_announcement (this->id, this->parent);
}

raddi::entry::announcement_type raddi::entry::is_announcement (const raddi::eid & id, const raddi::eid & parent) {
    if (id == parent) {
        if (id.timestamp == id.identity.timestamp) {
            return new_identity_announcement;
        } else {
            return new_channel_announcement;
        }
    } else
        return not_an_announcement;
}
