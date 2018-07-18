#ifndef RADDI_IDENTITY_H
#define RADDI_IDENTITY_H

#include "raddi_channel.h"

namespace raddi {

    // identity
    //  - identity announcement header
    // 
    struct identity : public channel {

        // public key
        //  - used to verify validity of messages posted by the identity
        //    and to encrypt private messages to the identity (crypto_sign_ed25519_pk_to_curve25519)
        //
        std::uint8_t public_key [crypto_sign_ed25519_PUBLICKEYBYTES];

        // create
        //  - creates new identity announcement: ids, generates key and nonce
        //  - stores public key in 'public_key' member
        //  - returns secret key in 'private_key' (seed only)
        //     - note that 'public_key' must be appended to 'private_key' in order to create complete
        //       and usable secret key (crypto_sign_ed25519_SECRETKEYBYTES bytes)
        //
        bool create (std::uint8_t (&private_key) [crypto_sign_ed25519_SEEDBYTES]);

        // verify
        //  - verifies that the new identity announcement entry is valid, i.e.:
        //     - identity nonce match public key
        //     - is signed by private-key matching 'public_key'
        //       (by calling 'verify' of raddi::entry parent class)
        //  - 'size' specifies number of bytes of transported entry: raddi::entry header + data
        //  - provided entry MUST be validated first!
        //
        bool verify (std::size_t size) const;
        bool verify (std::size_t size, const std::uint8_t (&public_key) [crypto_sign_ed25519_PUBLICKEYBYTES]) const {
            // TODO: this is hack for 'sign_and_validate' in raddi.com.cpp, FIX THAT
            return this->verify (size);
        }

        // overhead_size
        //  - number of bytes added by 'identity' announcement header
        //
        static constexpr std::size_t overhead_size = sizeof (raddi::identity::public_key);

        // max_description_size
        //  - maximum length of identity description content following identity_announcement header
        //  - basically max content size minus space occupied by above introduced fields
        //  - note that this is further restricted by consensus value
        // 
        static constexpr std::size_t max_description_size = raddi::channel::max_description_size
                                                          - overhead_size;
    };
}

#endif
