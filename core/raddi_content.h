#ifndef RADDI_CONTENT_H
#define RADDI_CONTENT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raddi {

    // content
    //  - 
    //
    struct content {
        struct text_appearance {
            bool heading = false; // otherwise normal text
            bool paragraph = false; // new paragraph, otherwise continuation
            bool bold = false;
            bool italic = false;
            bool underline = false;
            signed char intensity = 0; // -3 (almost background), 0 normal, +1 white/black, +2 and bold

            enum font : unsigned char {
                sans = 0,
                serif = 1,
                monospace = 2,
                handwriting = 3,
                reserved = 4,
            } font = font::sans;

            enum color : unsigned char {
                neutral = 0,
                // TBD
            } color = color::neutral;
        };

        /*struct text : public text_appearance {
            std::wstring text;
        };

        struct vote { // : something <enum xxx>
            unsigned int code; // TODO: enum
            std::wstring name;
        };*/

        // analysis
        //  - break-down of binary data into set of pointers and descriptions
        //
        struct analysis;

        // summary
        //  - bit-field containing analysis of the content for fast search
        //  - a lot of bits are exclusive to each other, but that's okay
        //  - note that integral values of enum values here are not the same
        //    as predefined codes of relevant marks/tokens
        //  - all bits are 'std::uint64_t' so the compiler does not add padding
        //
        union summary {
            enum class encrypted : std::uint64_t {
                nothing         = 0,
                partially       = 1, // no plain 'text', there may still be 'edit' blocks
                completely      = 2,
            };
            enum class band_type : std::uint64_t {
                primary     = 0,
                title       = 1, // identity/channel/thread title set/edit, plain-text allowed
                header      = 2, // reserved
                footer      = 3, // reserved
                sidebar     = 4, // identity/channel only, set/edit to sidebar info
                shoutbox    = 5, // tweet-like feed
                other       = 14, // contains other band code or string-defined band
                multiple    = 15, // contains multiple band specs (not valid now)
            };
            enum class moderation : std::uint64_t {
                none        = 0,
                hide        = 1, // moderator/author hides this entry
                nuke        = 2, // moderator hides this entry and all descending entries
                ban         = 3, // moderator bans the user for this content (also hide)
                nsfw        = 4, // moderator designates as: not safe for work, i.e. porn and professionally inappropriate stuff
                nsfl        = 5, // moderator designates as: not safe for life, i.e. gore and gross stuff
                spoiler     = 6, // moderator marks whole as spoiler

                stick       = 8, // sticked to the top of the thread
                highlight   = 9, // moderator highlights this as good content

                move        = 12, // reassigned to differ parent
                junction    = 13, // a sub-tree beginning with an eid should be appended at this position
                other       = 14, // contains other code or string-defined moderation opcode
                multiple    = 15, // contains multiple moderation opcodes
            };
            enum class votes : std::uint64_t {
                none        = 0,
                up          = 1,
                down        = 2,
                informative = 3,
                funny       = 4,
                spam        = 5,
                other       = 6, // contains other code or string-defined vote
                multiple    = 7, // contains multiple vote codes
            };

            struct {
                band_type     band              : 4;
                encrypted     encryption        : 2; // 0 - nothing, 1 - partially, 2 - completely
                std::uint64_t private_message   : 1; // contains section encrypted by parent's author public key
                std::uint64_t stream            : 1; // this entry denotes stream and contains info necessary to receive it
                std::uint64_t reserved_technical_bits : 8;

                moderation    mod    : 4;
                votes         vote   : 3;
                std::uint64_t report : 1; // report (BACKSPACE 08) 
                std::uint64_t revert : 1; // revert (BEL 07)
                std::uint64_t ack    : 1; // acknowledged (ACK 06)
                std::uint64_t text   : 1; // contains any uncompressed plain text content
                std::uint64_t edit   : 1; // contains diffs to parent

                std::uint64_t binary : 1; // contains one or more binary attachments
                std::uint64_t images : 1; // contains one or more images
                std::uint64_t audio  : 1; // contains one or more audio attachments
                std::uint64_t archive  : 1; // contains one or more archive attachments (zip/xz/...)
                std::uint64_t document : 1; // contains one or more document attachments (pdf)
                std::uint64_t crypto : 1; // contains one or more crypto-currency transaction, TODO: address???

                std::uint64_t remaining_available_bits : 30;
            };
            std::uint64_t raw;

        public:
            summary & operator |= (const summary & other) {
                this->raw |= other.raw;
                return *this;
            }
        };

        static_assert (sizeof (summary) == sizeof (std::uint64_t));

        // analyze
        //  - breaks the entry content down to text+formatting and marks/tokens; into 'analysis' structure below
        //  - 'length' should not include PoW
        //
        static analysis analyze (const std::uint8_t * content, std::size_t length);

    public:
        struct result {
            summary summmary;
            // TODO: list of compressed blocks failed to decompress
        };

        // parse
        //  - turns references into strings, byte codes into enum values
        //  - decompresses compressed if used dictionary is known
        //
        result parse (const analysis &);
        result parse (const std::uint8_t * data, std::size_t size);

        // serialize
        //  - TODO
        //
        //std::size_t serialize (std::uint8_t * buffer, std::size_t length);
    };

    // analysis
    //  - first step in between binary data and resulting 'content' structure
    //  - note that attachments of 0xFC (compressed) are NOT uncomrpessed and included in analysis
    //
    struct content::analysis {

        struct text : public content::text_appearance {
            const std::uint8_t * begin = nullptr;
            const std::uint8_t * end = nullptr;
        };
        struct reference {
            std::uint16_t offset = 0;
            std::uint16_t length = 0;
            const std::uint8_t * string = nullptr;
            const std::uint8_t * string_end = nullptr;
            bool                 truncated = false;
        };
        struct token {
            std::uint8_t         type = 0; // VT, DLE, ...
            std::uint8_t         code = 0;
            std::uint16_t        insertion = 0;
            bool                 truncated = false;
            bool                 defined = false;
            const std::uint8_t * string = nullptr;
            const std::uint8_t * string_end = nullptr;
        };
        struct attachment {
            std::uint8_t         type = 0;
            std::uint16_t        insertion = 0; // index of 'text' before which this belongs
            bool                 truncated = false;
            bool                 defined = false;
            std::uint16_t        size = 0;
            const std::uint8_t * data = nullptr;
        };
        struct mark {
            std::uint8_t         type = 0; // TODO: enum
            std::uint16_t        insertion = 0; // index of 'text' before which this belongs
            bool                 defined = false;
        };
        struct stamp {
            std::uint8_t         type = 0; // SYN, EM, ... (TODO: enum)
            std::uint8_t         code = 0;
            std::uint16_t        insertion = 0; // index of 'text' before which this belongs
            bool                 truncated = false;
            bool                 defined = false;
            std::uint8_t         size = 0;
            const std::uint8_t * data = nullptr;
        };

        // nested analysis for decompressed data
        //  - TODO: in valid entries, there should be only a single 'vote', 'sideband' etc. token,
        //          thus it'd make sense to have the one here, not to allocate the vector, perhaps
        //          devise some union { item; vector <item>; } type

        std::vector <text>      text;
        std::vector <reference> edits;
        std::vector <mark>      marks; // VT, DLE
        std::vector <token>     tokens; // ETX, BEL, BS
        std::vector <stamp>     stamps; // SYN, EM
        std::vector <attachment> attachments;

    public:
        // summarize
        //  - fills summary bit-field according to analysis content
        //
        summary summarize (summary = summary ()) const;
    };
}

#endif
