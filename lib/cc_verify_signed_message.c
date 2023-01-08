#include "cc_verify_signed_message.h"

#include "trezor-crypto/ecdsa.h"
#include "trezor-crypto/address.h"
#include "trezor-crypto/secp256k1.h"
#include "trezor-crypto/base58.h"
#include "trezor-crypto/cash_addr.h"
#include "trezor-crypto/blake256.h"
#include "trezor-crypto/ripemd160.h"

#include <sodium.h>
#include <string.h>

static size_t verify_address_checksum (cc_type type, const char * address, const uint8_t * data, size_t size, uint32_t checksum) {
    union {
        crypto_hash_sha256_state sha256;
        BLAKE256_CTX blake256;
    } ctx;

    uint8_t hash [32];
    switch (type) {
        case Bitcoin:
            crypto_hash_sha256_init (&ctx.sha256);
            crypto_hash_sha256_update (&ctx.sha256, data, size);
            crypto_hash_sha256_final (&ctx.sha256, hash);

            crypto_hash_sha256_init (&ctx.sha256);
            crypto_hash_sha256_update (&ctx.sha256, hash, 32);
            crypto_hash_sha256_final (&ctx.sha256, hash);
            break;
        case BitcoinCash:
            memset (hash, 0, sizeof hash);
            break;
        case Decred:
            blake256_Init (&ctx.blake256);
            blake256_Update (&ctx.blake256, data, size);
            blake256_Final (&ctx.blake256, hash);
            
            blake256_Init (&ctx.blake256);
            blake256_Update (&ctx.blake256, hash, 32);
            blake256_Final (&ctx.blake256, hash);
            break;
    }
    return base58_decoded_check (data, size, checksum, hash, address) >= 0;
}

static size_t verified_address_to_bytes (cc_type type, const char * address, uint8_t * buffer, uint32_t prefix) {
    uint32_t checksum;
    size_t prefix_length = ecdsa_address_prefix_bytes_len (prefix);
    size_t length = base58_decode (address, buffer, 20 + prefix_length, &checksum);

    if ((length == 20 + prefix_length) && ecdsa_address_check_prefix (buffer, prefix))
        if (verify_address_checksum (type, address, buffer, length, checksum))
            return length;

    return 0;
}

size_t cc_address_to_bytes (cc_type type, const char * address, uint8_t buffer [65]) {
    switch (type) {
        case Bitcoin:
            return verified_address_to_bytes (type, address, buffer, 0);
        case Decred:
            return verified_address_to_bytes (type, address, buffer, 1855);

        case BitcoinCash:
            char extended [MAX_ADDR_SIZE];
            if (strncmp (address, "bitcoincash", 10) != 0) {
                strcpy (extended, "bitcoincash");
                extended [11] = ':';
                strncpy (extended + 12, address, MAX_ADDR_SIZE - 12);

                address = extended;
            }
            return cash_addr_decode (buffer, "bitcoincash", address);
    }
    return 0;
}

static size_t ser_length (size_t len, uint8_t * out) {
    if (len < 253) {
        out [0] = len & 0xFF;
        return 1;
    }
    if (len < 0x10000) {
        out [0] = 253;
        out [1] = len & 0xFF;
        out [2] = (len >> 8) & 0xFF;
        return 3;
    }
#ifdef _WIN64
    if (len < 0x100000000) {
#endif
        out [0] = 254;
        out [1] = len & 0xFF;
        out [2] = (len >> 8) & 0xFF;
        out [3] = (len >> 16) & 0xFF;
        out [4] = (len >> 24) & 0xFF;
        return 5;
#ifdef _WIN64
    }
    out [0] = 255;
    out [1] = len & 0xFF;
    out [2] = (len >> 8) & 0xFF;
    out [3] = (len >> 16) & 0xFF;
    out [4] = (len >> 24) & 0xFF;
    out [5] = (len >> 32) & 0xFF;
    out [6] = (len >> 40) & 0xFF;
    out [7] = (len >> 48) & 0xFF;
    out [8] = (len >> 56) & 0xFF;
    return 9;
#endif
}

void cc_get_message_hash (cc_type type, const uint8_t * message, size_t message_length, uint8_t hash [32]) {
    uint8_t vi_message_length [9] = { 0 };
    size_t vi_message_length_cb = ser_length (message_length, vi_message_length);

    union {
        crypto_hash_sha256_state sha256;
        BLAKE256_CTX blake256;
    } ctx;

    switch (type) {
        case Bitcoin:
        case BitcoinCash:
            crypto_hash_sha256_init (&ctx.sha256);
            crypto_hash_sha256_update (&ctx.sha256, (const uint8_t *) "\x18""Bitcoin Signed Message:\n", 25);
            crypto_hash_sha256_update (&ctx.sha256, vi_message_length, vi_message_length_cb);
            crypto_hash_sha256_update (&ctx.sha256, message, message_length);
            crypto_hash_sha256_final (&ctx.sha256, hash);

            crypto_hash_sha256_init (&ctx.sha256);
            crypto_hash_sha256_update (&ctx.sha256, hash, 32);
            crypto_hash_sha256_final (&ctx.sha256, hash);
            break;

        case Decred:
            blake256_Init (&ctx.blake256);
            blake256_Update (&ctx.blake256, (const uint8_t *) "\x17""Decred Signed Message:\n", 24);
            blake256_Update (&ctx.blake256, vi_message_length, vi_message_length_cb);
            blake256_Update (&ctx.blake256, message, message_length);
            blake256_Final (&ctx.blake256, hash);
            break;
    }
}

int cc_verify_signed_message (cc_type type, const uint8_t hash [32], const uint8_t address [65], const uint8_t signature [65]) {
    union {
        crypto_hash_sha256_state sha256;
        BLAKE256_CTX blake256;
        RIPEMD160_CTX ripemd160;
    } ctx;

    uint8_t recovered_pubkey [65] = { 0 };
    if (ecdsa_recover_pub_from_sig (&secp256k1, recovered_pubkey, signature + 1, hash, (signature [0] - 27) % 4) == 0) {

        size_t pklen = 65;
        if (signature [0] >= 31) {
            recovered_pubkey [0] = 0x02 | (recovered_pubkey [64] & 1);
            pklen = 33;
        }

        uint32_t prefix = 0;
        switch (type) {
            case Decred:
                prefix = 1855;
        }

        uint8_t recovered [MAX_ADDR_RAW_SIZE] = { 0 };
        size_t offset = ecdsa_address_write_prefix_bytes (prefix, recovered);

        switch (type) {
            case Bitcoin:
            case BitcoinCash:
                crypto_hash_sha256_init (&ctx.sha256);
                crypto_hash_sha256_update (&ctx.sha256, recovered_pubkey, pklen);
                crypto_hash_sha256_final (&ctx.sha256, recovered + offset);
                break;

            case Decred:
                blake256_Init (&ctx.blake256);
                blake256_Update (&ctx.blake256, recovered_pubkey, pklen);
                blake256_Final (&ctx.blake256, recovered + offset);
                break;
        }

        ripemd160_Init (&ctx.ripemd160);
        ripemd160_Update (&ctx.ripemd160, recovered + offset, 32);
        ripemd160_Final (&ctx.ripemd160, recovered + offset);

        return memcmp (address, recovered, 20 + offset) == 0;
    } else
        return 0;
}


