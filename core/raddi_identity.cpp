#include "raddi_identity.h"
#include "raddi_protocol.h"

namespace {
    static inline std::uint64_t rotl64 (const std::uint64_t x, const int b) {
        return (x << b) | (x >> (64 - b));
    }
    static inline void round (std::uint64_t (&v) [4], std::size_t n) {
        while (n--) {
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
    }
    static inline std::uint32_t hash (std::uint32_t timestmap, const std::uint8_t (&key) [crypto_sign_ed25519_PUBLICKEYBYTES]) {
        std::uint64_t v [4];
        std::memcpy (v, key, sizeof v);

        static_assert (sizeof raddi::protocol::magic >= sizeof (std::uint64_t));
        static_assert (sizeof key >= sizeof v);

        v [3] ^= timestmap;
        round (v, 2);
        v [0] ^= timestmap;

        v [3] ^= *reinterpret_cast <std::uint64_t *> (&raddi::protocol::magic [0]);
        round (v, 2);
        v [0] ^= *reinterpret_cast <std::uint64_t *> (&raddi::protocol::magic [0]);

        v [3] ^= 12uLL << 56;
        round (v, 2);
        v [0] ^= 12uLL << 56;

        v [2] ^= 0xff;
        round (v, 4);
        
        auto h = v [0] ^ v [1] ^ v [2] ^ v [3];
        return (h >> 0) ^ (h >> 32);
    }
}

bool raddi::identity::create (std::uint8_t (&private_key) [crypto_sign_ed25519_SEEDBYTES]) {
    unsigned char sk [crypto_sign_ed25519_SECRETKEYBYTES];
    if (crypto_sign_ed25519_keypair (this->public_key, sk) == 0) {

        // return only the signing seed (private part of the key, first 32 bytes)
        // the applications retrieve the public part from database when needed

        std::memcpy (private_key, sk, sizeof private_key);
        sodium_memzero (sk, sizeof sk);

        // generate unique identifying nonce by hashing the public key
        //  - this prevents creation of, likely coliding, vanity nonces
        //  - using simplified siphash24 above, no security requirements
        //     - TODO: we also use simplified siphash in raddi::proof, consider merging
        
        this->id.timestamp = raddi::now ();
        this->id.identity.timestamp = this->id.timestamp;
        this->id.identity.nonce = hash (this->id.identity.timestamp, this->public_key);
        this->parent = this->id;
        return true;
    } else
        return false;
}

bool raddi::identity::verify (std::size_t size) const {
    return this->id.identity.nonce == hash (this->id.identity.timestamp, this->public_key)
        && this->entry::verify (size, nullptr, 0, this->public_key);
}