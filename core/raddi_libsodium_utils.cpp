#include "raddi_libsodium_utils.h"
#include "../common/platform.h"

#include <sodium.h>

#ifdef _WIN32
#ifdef  CRT_STATIC
extern "C" size_t crypto_aead_aegis256_maclen;
#endif
#endif

bool fast_crypto_aead_aegis256_available = false;
bool fast_crypto_aead_aes256gcm_available = false;;

void raddi::initial_check_for_fast_crypto () {
    if (sodium_runtime_has_armcrypto () || sodium_runtime_has_aesni ()) {

#ifdef _WIN32
#ifdef  CRT_STATIC
        crypto_aead_aegis256_maclen = 16;
#else
        if (auto hLibSodiumDLL = GetModuleHandle (L"LIBSODIUM")) {
            size_t * crypto_aead_aegis256_maclen = nullptr;
            if (Symbol (hLibSodiumDLL, crypto_aead_aegis256_maclen, "crypto_aead_aegis256_maclen")) {
                *crypto_aead_aegis256_maclen = 16;
            }
        }
#endif
#endif
        // libsodium 1.0.20 extends 'crypto_aead_aegis256_ABYTES' to 32 and breaks RADDI
        //  - we have custom libsodium which we can override back to 16 bytes, see code above
        //  - if that override doesn't work, it's libsodium from other source, thus bail and use GCM

        if (crypto_aead_aegis256_abytes () == 16) {
            fast_crypto_aead_aegis256_available = true;
        }
    }

    fast_crypto_aead_aes256gcm_available = crypto_aead_aes256gcm_is_available ();
}
