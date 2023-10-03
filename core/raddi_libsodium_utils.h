#ifndef RADDI_LIBSODIUM_UTILS_H
#define RADDI_LIBSODIUM_UTILS_H

namespace raddi {
    void initial_check_for_fast_crypto ();
}

extern bool fast_crypto_aead_aegis256_available;
extern bool fast_crypto_aead_aes256gcm_available;

#endif
