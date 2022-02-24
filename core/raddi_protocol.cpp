#include "raddi_protocol.h"
#include "raddi_timestamp.h"
#include "raddi_consensus.h"
#include "../lib/cuckoocycle.h"

alignas (std::uint64_t) char raddi::protocol::magic [8] = "RADDI/0";
enum raddi::protocol::aes256gcm_mode raddi::protocol::aes256gcm_mode = raddi::protocol::aes256gcm_mode::automatic;

const char * const raddi::protocol::aegis256::name = "AEGIS-256";
const char * const raddi::protocol::aes256gcm::name = "AES256-GCM";
const char * const raddi::protocol::xchacha20poly1305::name = "XChaCha20-Poly1305";

const char * raddi::protocol::aegis256::reveal () const { return this->name; }
const char * raddi::protocol::aes256gcm::reveal () const { return this->name; }
const char * raddi::protocol::xchacha20poly1305::reveal () const { return this->name; }

raddi::protocol::proposal::~proposal () {
    sodium_memzero (static_cast <keyset *> (this), sizeof (keyset));
}
raddi::protocol::aegis256::~aegis256 () {
    sodium_memzero (static_cast <keyset *> (this), sizeof (keyset));
}
raddi::protocol::aes256gcm::~aes256gcm () {
    sodium_memzero (&this->inbound_key, sizeof this->inbound_key);
    sodium_memzero (&this->outbound_key, sizeof this->outbound_key);
    sodium_memzero (&this->inbound_nonce, sizeof this->inbound_nonce);
    sodium_memzero (&this->outbound_nonce, sizeof this->outbound_nonce);
}
raddi::protocol::xchacha20poly1305::~xchacha20poly1305 () {
    sodium_memzero (static_cast <keyset *> (this), sizeof (keyset));
}

void raddi::protocol::initial::flags::pair::encode (std::uint32_t value) {
    randombytes_buf (&this->a, sizeof this->a);
    this->b = this->a ^ value;
}

void raddi::protocol::proposal::propose (initial * head) {
    randombytes_buf (this->inbound_key, sizeof this->inbound_key);
    randombytes_buf (this->outbound_key, sizeof this->outbound_key);
    randombytes_buf (this->inbound_nonce, sizeof this->inbound_nonce);
    randombytes_buf (this->outbound_nonce, sizeof this->outbound_nonce);
    
    crypto_scalarmult_base (head->keys.inbound_key, this->inbound_key);
    crypto_scalarmult_base (head->keys.outbound_key, this->outbound_key);

    std::memcpy (head->keys.inbound_nonce, this->inbound_nonce, sizeof this->inbound_nonce);
    std::memcpy (head->keys.outbound_nonce, this->outbound_nonce, sizeof this->outbound_nonce);

    std::uint32_t aes = 0;
    if (aes256gcm_mode != aes256gcm_mode::disabled) {
        switch (aes256gcm_mode) {
            case aes256gcm_mode::force_gcm:
                aes = 0x01;
                break;
            case aes256gcm_mode::force_aegis:
                aes = 0x02;
                break;
            default: // automatic or forced
                if (crypto_aead_aegis256_is_available ()) aes |= 0x02;
                if (crypto_aead_aes256gcm_is_available ()) aes |= 0x01;
                break;
        }
    }

    head->flags.hard.encode (0);
    head->flags.soft.encode (aes);
    head->timestamp = raddi::microtimestamp () ^ *reinterpret_cast <std::uint64_t *> (head->keys.inbound_key);
    
    cuckoo::hash <2,4> hash;
    hash.seed (head->keys.inbound_key, (const std::uint8_t *) &raddi::protocol::magic [0], sizeof raddi::protocol::magic);

    head->checksum = hash (head, sizeof (initial) - sizeof (initial::checksum));
}

raddi::protocol::encryption * raddi::protocol::proposal::accept (const raddi::protocol::initial * peer, accept_fail_reason * failure) {
    unsigned char rcvscalarmul [crypto_scalarmult_BYTES];
    unsigned char trmscalarmul [crypto_scalarmult_BYTES];

    crypto_scalarmult (rcvscalarmul, this->inbound_key, peer->keys.outbound_key);
    crypto_scalarmult (trmscalarmul, this->outbound_key, peer->keys.inbound_key);
    crypto_generichash (this->inbound_key, sizeof this->inbound_key, rcvscalarmul, sizeof rcvscalarmul,
                        reinterpret_cast <const unsigned char *> (raddi::protocol::magic), sizeof raddi::protocol::magic);
    crypto_generichash (this->outbound_key, sizeof this->outbound_key, trmscalarmul, sizeof trmscalarmul,
                        reinterpret_cast <const unsigned char *> (raddi::protocol::magic), sizeof raddi::protocol::magic);

    sodium_memzero (rcvscalarmul, sizeof rcvscalarmul);
    sodium_memzero (trmscalarmul, sizeof trmscalarmul);
    
    cuckoo::hash <2, 4> hash;
    hash.seed (peer->keys.inbound_key, (const std::uint8_t *) &raddi::protocol::magic [0], sizeof raddi::protocol::magic);

    if (peer->checksum != hash (peer, sizeof (initial) - sizeof (initial::checksum))) {
        *failure = accept_fail_reason::checksum;
        return nullptr;
    }

    auto peertime = peer->timestamp ^ *reinterpret_cast <const std::uint64_t *> (peer->keys.inbound_key);
    if (std::abs ((std::int64_t) (peertime - raddi::microtimestamp ())) > (1000'000 * raddi::consensus::max_entry_skew_allowed)) {
        *failure = accept_fail_reason::time;
        return nullptr;
    }

    if (peer->flags.hard.decode () != 0) {
        *failure = accept_fail_reason::flags;
        return nullptr;
    }

    if (aes256gcm_mode != aes256gcm_mode::disabled) {
        if ((peer->flags.soft.decode () & 0x0000'0002) && (aes256gcm_mode != aes256gcm_mode::force_gcm) && crypto_aead_aegis256_is_available ()) {
            *failure = accept_fail_reason::succeeded;
            return new aegis256 (this, &peer->keys);
        }
        if ((peer->flags.soft.decode () & 0x0000'0001) && (aes256gcm_mode != aes256gcm_mode::force_aegis) && crypto_aead_aes256gcm_is_available ()) {
            *failure = accept_fail_reason::succeeded;
            return new aes256gcm (this, &peer->keys);
        }

        switch (aes256gcm_mode) {
            case aes256gcm_mode::forced:
            case aes256gcm_mode::force_gcm:
            case aes256gcm_mode::force_aegis:
                *failure = accept_fail_reason::aes;
                return nullptr;
        }
    }

    *failure = accept_fail_reason::succeeded;
    return new xchacha20poly1305 (this, &peer->keys);
}

raddi::protocol::aegis256::aegis256 (const proposal * local, const keyset * peer) {
    static_cast <keyset &> (*this) = *local;

    sodium_add (this->inbound_nonce, peer->outbound_nonce, sizeof this->inbound_nonce);
    sodium_add (this->outbound_nonce, peer->inbound_nonce, sizeof this->outbound_nonce);
}

raddi::protocol::aes256gcm::aes256gcm (const proposal * local, const keyset * peer) {
    crypto_aead_aes256gcm_beforenm (&this->inbound_key, local->inbound_key);
    crypto_aead_aes256gcm_beforenm (&this->outbound_key, local->outbound_key);

    std::memcpy (this->inbound_nonce, local->inbound_nonce, sizeof this->inbound_nonce);
    std::memcpy (this->outbound_nonce, local->outbound_nonce, sizeof this->outbound_nonce);

    sodium_add (this->inbound_nonce, peer->outbound_nonce, sizeof this->inbound_nonce);
    sodium_add (this->outbound_nonce, peer->inbound_nonce, sizeof this->outbound_nonce);
}

raddi::protocol::xchacha20poly1305::xchacha20poly1305 (const proposal * local, const keyset * peer) {
    static_cast <keyset &> (*this) = *local;

    sodium_add (this->inbound_nonce, peer->outbound_nonce, sizeof this->inbound_nonce);
    sodium_add (this->outbound_nonce, peer->inbound_nonce, sizeof this->outbound_nonce);
}

std::size_t raddi::protocol::aegis256::encode (unsigned char * message, std::size_t max,
                                               const unsigned char * data, std::size_t size) {
    if ((size <= raddi::protocol::max_payload) && (size + raddi::protocol::frame_overhead <= max)) {

        auto length = size + crypto_aead_aegis256_ABYTES;
        message [0] = (length >> 0) & 0xFF;
        message [1] = (length >> 8) & 0xFF;

        sodium_increment (this->outbound_nonce, sizeof this->outbound_nonce);
        if (crypto_aead_aegis256_encrypt (&message [2], nullptr, data, size, &message [0], 2,
                                          nullptr, this->outbound_nonce, this->outbound_key) == 0)
            return length + sizeof (std::uint16_t);
    }
    return 0;
}

std::size_t raddi::protocol::aes256gcm::encode (unsigned char * message, std::size_t max,
                                                const unsigned char * data, std::size_t size) {
    if ((size <= raddi::protocol::max_payload) && (size + raddi::protocol::frame_overhead <= max)) {

        auto length = size + crypto_aead_aes256gcm_ABYTES;
        message [0] = (length >> 0) & 0xFF;
        message [1] = (length >> 8) & 0xFF;

        sodium_increment (this->outbound_nonce, sizeof this->outbound_nonce);
        if (crypto_aead_aes256gcm_encrypt_afternm (&message [2], nullptr, data, size, &message [0], 2,
                                                   nullptr, this->outbound_nonce, &this->outbound_key) == 0)
            return length + sizeof (std::uint16_t);
    }
    return 0;
}

std::size_t raddi::protocol::xchacha20poly1305::encode (unsigned char * message, std::size_t max,
                                                        const unsigned char * data, std::size_t size) {
    if ((size <= raddi::protocol::max_payload) && (size + raddi::protocol::frame_overhead <= max)) {

        auto length = size + crypto_aead_xchacha20poly1305_ietf_ABYTES;
        message [0] = (length >> 0) & 0xFF;
        message [1] = (length >> 8) & 0xFF;

        sodium_increment (this->outbound_nonce, sizeof this->outbound_nonce);
        if (crypto_aead_xchacha20poly1305_ietf_encrypt (&message [2], nullptr, data, size, &message [0], 2,
                                                        nullptr, this->outbound_nonce, this->outbound_key) == 0)
            return length + sizeof (std::uint16_t);
    }
    return 0;
}

std::size_t raddi::protocol::aegis256::decode (unsigned char * message, std::size_t max,
                                               const unsigned char * data, std::size_t size) {
    if ((size >= raddi::protocol::frame_overhead) && (size - raddi::protocol::frame_overhead <= max)) {
        unsigned long long length;
        sodium_increment (this->inbound_nonce, sizeof this->inbound_nonce);
        if (crypto_aead_aegis256_decrypt (message, &length, nullptr,
                                          &data [2], size - 2, &data [0], 2,
                                          this->inbound_nonce, this->inbound_key) == 0)
            return (std::size_t) length;
    }
    return 0;
}

std::size_t raddi::protocol::aes256gcm::decode (unsigned char * message, std::size_t max,
                                                const unsigned char * data, std::size_t size) {
    if ((size >= raddi::protocol::frame_overhead) && (size - raddi::protocol::frame_overhead <= max)) {
        unsigned long long length;
        sodium_increment (this->inbound_nonce, sizeof this->inbound_nonce);
        if (crypto_aead_aes256gcm_decrypt_afternm (message, &length, nullptr,
                                                   &data [2], size - 2, &data [0], 2,
                                                   this->inbound_nonce, &this->inbound_key) == 0)
            return (std::size_t) length;
    }
    return 0;
}

std::size_t raddi::protocol::xchacha20poly1305::decode (unsigned char * message, std::size_t max,
                                                        const unsigned char * data, std::size_t size) {
    if ((size >= raddi::protocol::frame_overhead) && (size - raddi::protocol::frame_overhead <= max)) {
        unsigned long long length;
        sodium_increment (this->inbound_nonce, sizeof this->inbound_nonce);
        if (crypto_aead_xchacha20poly1305_ietf_decrypt (message, &length, nullptr,
                                                        &data [2], size - 2, &data [0], 2,
                                                        this->inbound_nonce, this->inbound_key) == 0)
            return (std::size_t) length;
    }
    return 0;
}
