#include "raddi_protocol.h"
#include "raddi_timestamp.h"

alignas (std::uint64_t) char raddi::protocol::magic [8] = "RADDI/1";
enum raddi::protocol::aes256gcm_mode raddi::protocol::aes256gcm_mode = raddi::protocol::aes256gcm_mode::automatic;

const wchar_t * raddi::protocol::proposal::name () const {
    if (this->outbound_nonce [7] & 0x01) {
        return L"AES256-GCM";
    } else {
        return L"XChaCha20-Poly1305";
    }
}
const wchar_t * raddi::protocol::aes256gcm::name () const {
    return L"AES256-GCM";
}
const wchar_t * raddi::protocol::xchacha20poly1305::name () const {
    return L"XChaCha20-Poly1305";
}

raddi::protocol::proposal::~proposal () {
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

void raddi::protocol::proposal::propose (raddi::protocol::keyset * head) {
    randombytes_buf (this->inbound_key, sizeof this->inbound_key);
    randombytes_buf (this->outbound_key, sizeof this->outbound_key);
    randombytes_buf (this->inbound_nonce, sizeof this->inbound_nonce);
    randombytes_buf (this->outbound_nonce, sizeof this->outbound_nonce);
    
    if ((aes256gcm_mode != aes256gcm_mode::disabled) && crypto_aead_aes256gcm_is_available ()) {
        this->outbound_nonce [7] |= 0x01;
    } else {
        this->outbound_nonce [7] &= ~0x01;
    }

    crypto_scalarmult_base (head->inbound_key, this->inbound_key);
    crypto_scalarmult_base (head->outbound_key, this->outbound_key);

    std::memcpy (head->inbound_nonce, this->inbound_nonce, sizeof this->inbound_nonce);
    std::memcpy (head->outbound_nonce, this->outbound_nonce, sizeof this->outbound_nonce);
}

raddi::protocol::encryption * raddi::protocol::proposal::accept (const raddi::protocol::keyset * peer) {
    unsigned char rcvscalarmul [crypto_scalarmult_BYTES];
    unsigned char trmscalarmul [crypto_scalarmult_BYTES];

    crypto_scalarmult (rcvscalarmul, this->inbound_key, peer->outbound_key);
    crypto_scalarmult (trmscalarmul, this->outbound_key, peer->inbound_key);
    crypto_generichash (this->inbound_key, sizeof this->inbound_key, rcvscalarmul, sizeof rcvscalarmul,
                        reinterpret_cast <const unsigned char *> (raddi::protocol::magic), sizeof raddi::protocol::magic);
    crypto_generichash (this->outbound_key, sizeof this->outbound_key, trmscalarmul, sizeof trmscalarmul,
                        reinterpret_cast <const unsigned char *> (raddi::protocol::magic), sizeof raddi::protocol::magic);

    sodium_memzero (rcvscalarmul, sizeof rcvscalarmul);
    sodium_memzero (trmscalarmul, sizeof trmscalarmul);
    
    if ((aes256gcm_mode != aes256gcm_mode::disabled) && crypto_aead_aes256gcm_is_available () && (peer->outbound_nonce [7] & 0x01)) {
        return new aes256gcm (this, peer);
    } else {
        if (aes256gcm_mode != aes256gcm_mode::forced) {
            return new xchacha20poly1305 (this, peer);
        } else
            return nullptr;
    }
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

        auto length = size + crypto_aead_chacha20poly1305_ietf_ABYTES;
        message [0] = (length >> 0) & 0xFF;
        message [1] = (length >> 8) & 0xFF;

        sodium_increment (this->outbound_nonce, sizeof this->outbound_nonce);
        if (crypto_aead_chacha20poly1305_ietf_encrypt (&message [2], nullptr, data, size, &message [0], 2,
                                                       nullptr, this->outbound_nonce, this->outbound_key) == 0)
            return length + sizeof (std::uint16_t);
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
        if (crypto_aead_chacha20poly1305_ietf_decrypt (message, &length, nullptr,
                                                       &data [2], size - 2, &data [0], 2,
                                                       this->inbound_nonce, this->inbound_key) == 0)
            return (std::size_t) length;
    }
    return 0;
}
