#ifndef RADDI_PROTOCOL_H
#define RADDI_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <sodium.h>

namespace raddi {
    namespace protocol {

        // magic
        //  - string used to distinguish between incompatible protocol version
        //     - hashed into inbound/outbound keys ensuring only compatible peers connect
        //     - also used in local peer discovery to distinguish compatible peers
        //  - default is "RADDI/1\0"
        //
        extern char magic [8];

        // aes256gcm_mode
        //  - configures when to use HW AES256-GCM
        //
        enum class aes256gcm_mode {
            automatic, // HW AES used when supported by both parties, XChaCha20-Poly1305 when not
            disabled, // AES is not reported to other peers and thus never used
            forced // AES is always used, peers that don't support AES are disconnected
        };
        extern enum aes256gcm_mode aes256gcm_mode;
        
        // keyset
        //  - stores aligned inbound/outbound key/nonce pairs
        //
        struct CRYPTO_ALIGN (16) keyset {

            // (in/out)bound_key (32 bytes)
            //  - public parts of private link encryption keys, exchanged usings D-H (crypto_scalarmult)
            // 
            std::uint8_t inbound_key [std::max (crypto_aead_xchacha20poly1305_ietf_KEYBYTES, crypto_aead_aes256gcm_KEYBYTES)];
            std::uint8_t outbound_key [std::max (crypto_aead_xchacha20poly1305_ietf_KEYBYTES, crypto_aead_aes256gcm_KEYBYTES)];

            // (in/out)bound_nonce (24 bytes, XChaCha20-Poly1305 nonces are the largest now)
            //  - part of a link encryption nonce, added to peer's part to establish actual nonce used
            //
            std::uint8_t inbound_nonce [std::max (crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, crypto_aead_aes256gcm_NPUBBYTES)];
            std::uint8_t outbound_nonce [std::max (crypto_aead_xchacha20poly1305_ietf_NPUBBYTES, crypto_aead_aes256gcm_NPUBBYTES)];
        };

        // initial
        //  - sent as communication header, first data block exchanged, proposal that carries one part of D-H negotiation
        //
        struct initial {
            keyset keys;

            // flags
            //  - 'soft' flags are options that can be refused or ignored
            //  - 'hard' flags are breaking changes; unknown set hard flag means disconnect
            //
            struct flags {
                struct pair {
                    std::uint32_t a;
                    std::uint32_t b;

                    std::uint32_t decode () const { return this->a ^ this->b; }
                    void          encode (std::uint32_t value); // non-deterministic
                } soft
                , hard;
            } flags;
        };

        // encryption
        //  - base interface for aes256gcm and xchacha20poly1305 that handle p2p connection encryption
        //
        class encryption {
        public:
            virtual ~encryption () {};

            // encode
            //  - encrypts and frames 'size' bytes of 'data' into 'message' for transmission
            //  - returns length of the whole protocol frame (always 'size'+'frame_overhead')
            // 
            virtual std::size_t encode (unsigned char * message, std::size_t max, const unsigned char * data, std::size_t size) = 0;

            // decode
            //  - decrypts and unpacks 'data' frame of 'size' (next_frame_size) bytes into 'message'
            //
            virtual std::size_t decode (unsigned char * message, std::size_t max, const unsigned char * data, std::size_t size) = 0;

            // name
            //  - returns name of the encryption scheme
            //
            virtual const wchar_t * name () const = 0;
        };

        // proposal
        //  - created for every new connection, contains private parts of D-H key exchange
        //    and random nonces
        //  - replaced by 'accept'ed concrete encryption object
        //
        struct proposal : keyset {

            // propose
            //  - randomizes the proposal and generates communication 'head'
            // 
            void         propose (initial * head);

            // accept
            //  - finishes D-H and generates encryption object according to peer's proposal
            //
            encryption * accept (const initial * head);

            // name
            //  - returns name of encryption scheme being proposed
            //
            const wchar_t * name () const;

            // destructor clears keys from memory
            ~proposal ();
        };
        
        // aes256gcm
        //  - state for fast hardware (AES-NI) AES256-GCM encryption
        //
        class aes256gcm
            : public encryption {

            crypto_aead_aes256gcm_state inbound_key;
            crypto_aead_aes256gcm_state outbound_key;
            std::uint8_t inbound_nonce [crypto_aead_aes256gcm_NPUBBYTES];
            std::uint8_t outbound_nonce [crypto_aead_aes256gcm_NPUBBYTES];

            virtual std::size_t encode (unsigned char *, std::size_t, const unsigned char *, std::size_t) override;
            virtual std::size_t decode (unsigned char *, std::size_t, const unsigned char *, std::size_t) override;
            virtual const wchar_t * name () const override;

        public:
            explicit aes256gcm (const proposal * proposal, const keyset * peer);
            virtual ~aes256gcm ();
        };

        // xchacha20poly1305
        //  - state for software implementation of encryption
        //  - reusing 'keyset' to simplify the code; data of XChaCha20-Poly1305 are largest now
        //
        class xchacha20poly1305
            : public encryption
            , private keyset {
        
            virtual std::size_t encode (unsigned char *, std::size_t, const unsigned char *, std::size_t) override;
            virtual std::size_t decode (unsigned char *, std::size_t, const unsigned char *, std::size_t) override;
            virtual const wchar_t * name () const override;

        public:
            explicit xchacha20poly1305 (const proposal * proposal, const keyset * peer);
            virtual ~xchacha20poly1305 ();
        };

        // frame_overhead
        //  - protocol frame overhead, i.e. how many bytes 'encode' adds
        //
        static constexpr std::size_t frame_overhead = sizeof (std::uint16_t)
                                                    + std::max (crypto_aead_aes256gcm_ABYTES, crypto_aead_xchacha20poly1305_ietf_ABYTES);

        // max_payload
        //  - maximum data encoded inside the protocol frame
        //  - NOTE: -1 disallows 65519 bytes long payload that would require 65537 bytes long allocations,
        //          this conveniently reserves 0xFFFF token to represent keep-alive response (to 0x0000 query)
        //
        static constexpr std::size_t max_payload = UINT16_MAX
                                                 - frame_overhead
                                                 + sizeof (std::uint16_t) - 1;

        // max_frame_size
        //  - maximum size of encrypted data frame (all inclusive) transmitted over the protocol
        //
        static constexpr std::size_t max_frame_size = frame_overhead
                                                    + max_payload
                                                    ;
    }
}

#endif
