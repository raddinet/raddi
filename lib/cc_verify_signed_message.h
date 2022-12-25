#ifndef RADDI_CC_VERIFY_SIGNED_MESSAGE_H
#define RADDI_CC_VERIFY_SIGNED_MESSAGE_H

// interface towards modified trezor-firmware C crypto code

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    Bitcoin,
    BitcoinCash,
    Decred,
} cc_type;

// cc_address_to_bytes
//  - 
//  - for GUI
//  - TODO: enter address + signature; Apps displays text to sign, App validates
//  - note: signature is serialized as Base64
//
size_t cc_address_to_bytes (cc_type type, const char * address, uint8_t buffer [65]);

// cc_get_message_hash
//  - 
//
void cc_get_message_hash (cc_type type, const char * message, uint8_t hash [32]);

// cc_verify_signed_message
//  -
//
int cc_verify_signed_message (cc_type type, const uint8_t hash [32], const uint8_t address [65], const uint8_t signature [65]);

#ifdef __cplusplus
}
#endif

#endif
