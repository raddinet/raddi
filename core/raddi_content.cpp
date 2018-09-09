#include"raddi_content.h"
#include"raddi_eid.h"
#include <lzma.h>

raddi::content::analysis raddi::content::analyze (const std::uint8_t * content, std::size_t length) {
    content::analysis analysis;
    text_appearance state;
    const std::uint8_t * text_start = nullptr;

    auto finish_text = [&content, &text_start, &analysis, &state] () {
        if ((text_start != nullptr) && (content != text_start)) {
            analysis.text.push_back ({ state, text_start, content });
            text_start = nullptr;
            state.paragraph = false;
        }
    };

    while (length) {
        if ((*content <= 0x1F) || (*content == 0x7F) || (*content >= 0xF8)) {
            switch (*content) {

                case 0x00: // NOP, too early, 'length' should not include PoW, still we are done
                    finish_text ();
                    return analysis;

                case 0x09: // TAB, used in text formatting
                case 0x0D: // CR, used in text formatting, TODO: replace CR with CR-LF for Windows
                    if (text_start == nullptr) {
                        text_start = content;
                    }
                    break;

                // most control bytes and bytes disalowed within UTF-8 have specific meaning
                // and they need to be removed from the displayed text; since analysis only
                // generates pointers into the content, every such byte breaks a new 'text'
                
                default:
                    finish_text ();
                    break;
            }
        } else {
            if (text_start == nullptr) {
                text_start = content;
            }
        }

        switch (*content) {
            case 0x01: // SOH, new heading
                state = text_appearance ();
                state.heading = true;
                state.paragraph = true;
                break;
            case 0x02: // STX, start of text (end of heading)
                state = text_appearance ();
                state.paragraph = true;
                break;
            case 0x0A:
                state.paragraph = true;
                break;

            case 0x18: // CAN, cancel formatting
                state = text_appearance ();
                break;

            case 0x03: // ETX, end of section
            case 0x04: // EOT, // reserved
            case 0x05: // ENQ, // reserved
            case 0x06: // ACK, acknowledged by recepient
            case 0x07: // BEL, report
            case 0x08: // BS,  revert
            case 0x0C: // FF,  // reserved
            case 0x17: // ETB, // reserved
            case 0x19: // EM,  // reserved
            case 0x1C: // FS, table  --.
            case 0x1D: // GS, tab (?)--+-- used to render tables
            case 0x1E: // RS, row    --|
            case 0x1F: // US, column --'
            case 0x7F: // DEL, delete, mod-op
                analysis.markers.push_back ({ *content, (std::uint16_t) analysis.text.size () });
                break;

            case 0x0B: // VT, vote token
            case 0x10: // DLE, sideband marking token
            case 0x15: // NAK, moderation ops
            case 0x1A: // SUB, // reserved
                if (length > 1) {
                    auto p = content + 1;
                    auto n = length - 1;

                    analysis::token token;
                    token.type = *content;
                    token.insertion = (std::uint16_t) analysis.text.size ();

                    if (*p < 0x20) {
                        token.code = *p;
                    } else {
                        token.string = p;
                        do {
                            ++p;
                            --n;
                        } while (n && *p);
                        token.string_end = p;

                        if (!n) {
                            token.truncated = true;
                        }
                    }
                    analysis.tokens.push_back (token);
                    length -= (p - content);
                    content = p;

                    if (!n)
                        continue;
                }
                break;

            case 0x0E: // SO
                --state.intensity;
                break;
            case 0x0F: // SI
                ++state.intensity;
                break;

            case 0x11: state.font = text_appearance::font::serif; break;
            case 0x12: state.font = text_appearance::font::monospace; break;
            case 0x13: state.font = text_appearance::font::handwriting; break;
            case 0x14: state.font = text_appearance::font::reserved; break;

            case 0x16: // SYN, chaining, TODO
                break;

            case 0x1B: // ESC, reserved for ANSI escape codes, skip most of them now
                if (length > 1) {
                    auto begin = content + 1;

                    // skip all until byte in 0x40–0x7E range, that one terminates the sequence

                    do {
                        ++content;
                        --length;
                    } while ((length > 1) && ((*content < 0x40) || (*content > 0x7E)));

                    auto end = content;

                    switch (*begin) {
                        case '~': // Invoke the G1 Character Set
                            state.font = text_appearance::font::serif;
                            break;
                        case 'N': // SS2 – Single Shift Two
                            state.font = text_appearance::font::monospace;
                            break;
                        case 'O': // SS3 – Single Shift Three
                            state.font = text_appearance::font::handwriting;
                            break;
                        case 'c': // RIS – Reset to Initial State
                            state = text_appearance ();
                            break;
                        case '[': // CSI - Control Sequence Introducer
                            switch (*end) {
                                case 'm':
                                    // TODO: break [begin;end) into values separated by ';' and parse
                                    // http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
                                    // Color: ESC [ 38 ; 5 ; Ps m
                                    // Color: ESC [ 38 ; 2 ; Pi ; Pr ; Pg ; Pb m
                                    //  - Pi is optional

                                    /* TODO: Support:
                                    0	Reset / Normal	all attributes off
                                    1	Bold or increased intensity
                                    2	Faint (decreased intensity)
                                    3	Italic	Not widely supported. Sometimes treated as inverse.
                                    4	Underline
                                    7	reverse video	swap foreground and background colors
                                    8	Conceal	Not widely supported. - RADDI: THIS COULD BE USED FOR 'SPOILER' TAGS
                                    9	Crossed-out	Characters legible, but marked for deletion.
                                    10	Primary (default) font
                                    11–19	Alternative font	Select alternative font { \displaystyle n - 10 } {\displaystyle n - 10}
                                    20	Fraktur	Rarely supported
                                    21	Bold off
                                    22	Normal color or intensity	Neither bold nor faint
                                    23	Not italic, not Fraktur
                                    24	Underline off	Not singly or doubly underlined
                                    27	Inverse off
                                    28	Reveal	conceal off
                                    29	Not crossed out
                                    30–37	Set foreground color (table)
                                    38	Set foreground color	Next arguments are 5; n or 2; r; g; b
                                    39	Default foreground color	implementation defined (according to standard)
                                    40–47	Set background color	See color table below
                                    48	Set background color	Next arguments are 5; n or 2; r; g; b, see below
                                    49	Default background color	implementation defined (according to standard)
                                    */
                                    break;
                            }
                            break;
                        case ']': // OSC - Operating System Command
                            //switch (*end) {
                                // TODO: OSC Ps ; Pt ST
                                //  - Ps = 4 ; c ; spec -> Change Color Number c to the color specified by spec.
                                //  - Ps = 5 0  -> Set Font to Pt. If Pt begins with a "#", index in the font menu,
                                //                 relative (if the next character is a plus or minus sign) or absolute.
                                //  - Ps = 1 0 4 ; c -> Reset Color Number c.
                            //}
                            break;
                    }
                }
                break;

            case 0xF8:
            case 0xF9:
            case 0xFA:
            case 0xFB:
            case 0xFC:
            case 0xFD:
            case 0xFE:
            case 0xFF:
                if (length > 2) {
                    analysis::attachment attachment;
                    attachment.type = content [0];
                    attachment.data = content + 3;
                    attachment.size = content [1] | (content [2] << 8);
                    attachment.insertion = (std::uint16_t) analysis.text.size ();

                    if (attachment.size > length - 3) {
                        attachment.size = (std::uint16_t) (length - 3);

                        // allow FFFF as a fast way to say 'up to the end of the entry'

                        if ((content [1] != 0xFF) || (content [2] != 0xFF)) {
                            attachment.truncated = true;
                        }
                    }

                    switch (attachment.type) {
                        case 0xFA: // binary attachment
                        case 0xFC: // compressed data
                        case 0xFD: // differential content, edit
                        case 0xFE: // encrypted data
                        case 0xFF: // private message
                            attachment.known = true;
                    }

                    switch (attachment.type) {
                        case 0xFD: // differential content, edit, validate
                            if (attachment.size >= 4) {
                                analysis::reference edit;

                                edit.offset = attachment.data [0] | (attachment.data [1] << 8);
                                edit.length = attachment.data [2] | (attachment.data [3] << 8);
                                edit.string = attachment.data + 4;
                                edit.string_end = attachment.data + attachment.size;
                                edit.truncated = attachment.truncated;

                                analysis.edits.push_back (edit);
                                break;
                            } else {
                                attachment.truncated = true;
                            }
                            [[ fallthrough ]]; // if not inserted into 'edits' insert into attachments

                        case 0xFC: // compressed data, validate there are two dictionary bytes
                            if (attachment.size < 2) {
                                attachment.truncated = true;
                            }
                            [[ fallthrough ]]; // insert into attachments

                        default:
                            analysis.attachments.push_back (attachment);
                    }

                    content += attachment.size + 3;
                    length -= attachment.size + 3;
                    continue;
                }
                break;
        }

        ++content;
        --length;
    }
    finish_text ();
    return analysis;
}

namespace {
    template <typename T, std::size_t N>
    std::size_t parse_and_consume_prefixed_id (const char * data, std::size_t size, const char (&prefix) [N], T & value) {
        if (size >= (N - 1) + T::min_length) {
            if (std::strncmp (data, prefix, N - 1) == 0) {
                return value.parse (prefix + (N - 1));
            }
        }
        return 0;
    }

    template <typename T, std::size_t N>
    bool validate_prefixed_id (const char * data, std::size_t size, const char (&prefix) [N]) {
        T temporary;
        return parse_and_consume_prefixed_id (data, size, prefix, temporary) == size - (N - 1);
    }
}

raddi::content::summary raddi::content::analysis::summarize (summary summary) const {

    if (!this->text.empty ()) {
        summary.text = true;
    }
    if (!this->edits.empty ()) {
        summary.edit = true;
    }

    for (const auto & attachment : this->attachments) {

        // format/formats
        //  - list of known binary attachments to categorize
        //
        struct format {
            enum class type : std::uint8_t {
                document,
                archive,
                image,
                audio,
            } type;
            std::uint8_t length;
            std::uint8_t prefix [10];
        } formats [] = {
            { format::type::document, 4, { 0x25, 0x50, 0x44, 0x46 } }, // PDF
            { format::type::document, 5, { 0x3C, 0x3F, 0x78, 0x6D, 0x6C } }, // XML

            { format::type::archive, 2, { 0x50, 0x4B } }, // ZIP
            { format::type::archive, 4, { 0x52, 0x61, 0x72, 0x21 } }, // RAR
            { format::type::archive, 2, { 0x37, 0x7A } }, // 7z

            { format::type::image, 8, { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A } }, // PNG
            { format::type::image, 6, { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 } }, // GIF89a
            { format::type::image, 6, { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 } }, // GIF87a
            { format::type::image, 2, { 0xFF, 0xD8 } }, // JPG

            { format::type::audio, 4, { 0x4F, 0x67, 0x67, 0x53 } }, // OGG
            { format::type::audio, 4, { 0x4D, 0x54, 0x68, 0x64 } }, // MID
        };

        switch (attachment.type) {

            case 0xFA: // binary attachment
                summary.binary = true;

                for (const auto & format : formats) {
                    if ((attachment.size >= format.length) && (std::memcmp (attachment.data, format.prefix, format.length) == 0)) {
                        switch (format.type) {
                            case format::type::document: summary.document = true; break;
                            case format::type::archive: summary.archive = true; break;
                            case format::type::image: summary.images = true; break;
                            case format::type::audio: summary.audio = true; break;
                        }
                        break;
                    }
                }
                break;

            case 0xFC: // compressed data
                break;

            case 0xFE: // encrypted data
                if (this->text.empty ()) {
                    summary.encryption = summary::encrypted::completely;
                } else {
                    summary.encryption = summary::encrypted::partially;
                }
                break;

            case 0xFF: // private message
                summary.private_message = true;
                break;
        }
    }

    for (const auto & marker : this->markers) {
        switch (marker.type) {
            case 0x03: // ETX, end of section
                break;
            case 0x04: // EOT, // reserved
                break;
            case 0x05: // ENQ, // reserved
                break;
            case 0x06: // ACK, acknowledged by recepient
                summary.ack = true;
                break;
            case 0x07: // BEL, report
                summary.report = true;
                break;
            case 0x08: // BS,  revert
                summary.revert = true;
                break;
            case 0x0C: // FF,  // reserved
                break;
            case 0x17: // ETB, // reserved
                break;
            case 0x19: // EM,  // reserved
                break;
            case 0x1C: // FS, table  --.
            case 0x1D: // GS, tab (?)--+-- used to render tables
            case 0x1E: // RS, row    --|
            case 0x1F: // US, column --'
                break;
            case 0x7F: // DEL, delete, mod-op
                if (summary.mod == summary::moderation::none) {
                    summary.mod = summary::moderation::hide;
                } else {
                    summary.mod = summary::moderation::multiple;
                }
                break;
        }
    }

    for (const auto & token : this->tokens) {
        switch (token.type) {

            case 0x0B: // VT, vote token
                if (summary.vote != summary::votes::none) {
                    summary.vote = summary::votes::multiple;
                } else {
                    if (token.string) {
                        summary.vote = summary::votes::other;
                    } else {
                        switch (token.code) {
                            case 0x01: summary.vote = summary::votes::up; break;
                            case 0x02: summary.vote = summary::votes::down; break;
                            case 0x03: summary.vote = summary::votes::informative; break;
                            case 0x04: summary.vote = summary::votes::funny; break;
                            case 0x05: summary.vote = summary::votes::spam; break;
                            default:
                                summary.vote = summary::votes::other;
                                break;
                        }
                    }
                }
                break;

            case 0x10: // DLE, sideband marking token
                if (summary.band != summary::band_type::primary) {
                    summary.band = summary::band_type::multiple;
                } else {
                    if (token.string) {
                        summary.band = summary::band_type::other;
                    } else {
                        switch (token.code) {
                            case 0x01: summary.band = summary::band_type::title; break;
                            case 0x02: summary.band = summary::band_type::header; break;
                            case 0x03: summary.band = summary::band_type::footer; break;
                            case 0x04: summary.band = summary::band_type::sidebar; break;
                            case 0x05: summary.band = summary::band_type::shoutbox; break;
                            default:
                                summary.band = summary::band_type::other;
                                break;
                        }
                    }
                }
                break;

            case 0x15: // NAK, moderation ops
                if (summary.mod != summary::moderation::none) {
                    summary.mod = summary::moderation::multiple;
                } else {
                    if (token.string) {
                        summary.mod = summary::moderation::other; // default

                        if (!token.truncated) { // ensures 'string_end' points to NUL-terminating byte
                            auto length = token.string_end - token.string;
                            auto text = (const char *) token.string;

                            // validate move/juncrion/...
                            //  - same format for all these: "prefix:abcdefab0-0" to "prefix:abcdefab01234567-01234567"

                            if (validate_prefixed_id <eid> (text, length, "move:")) {
                                summary.mod = summary::moderation::move;
                            }
                            if (validate_prefixed_id <eid> (text, length, "junction:")) {
                                summary.mod = summary::moderation::junction;
                            }
                        }
                    } else {
                        switch (token.code) {
                            case 0x01: summary.mod = summary::moderation::hide; break; // use DEL marker instead
                            case 0x02: summary.mod = summary::moderation::nuke; break;
                            case 0x03: summary.mod = summary::moderation::ban; break;
                            case 0x04: summary.mod = summary::moderation::nsfw; break;
                            case 0x05: summary.mod = summary::moderation::nsfl; break;
                            case 0x06: summary.mod = summary::moderation::spoiler; break;
                            case 0x08: summary.mod = summary::moderation::stick; break;
                            case 0x09: summary.mod = summary::moderation::highlight; break;
                            default:
                                summary.mod = summary::moderation::other;
                                break;
                        }
                    }
                }
                break;

            case 0x1A: // SUB, // reserved
                break;
        }
    }

    return summary;
}

raddi::content::result raddi::content::parse (const std::uint8_t * data, std::size_t size) {
    return this->parse (content::analyze (data, size));
}

raddi::content::result raddi::content::parse (const analysis &) {
    
    // TODO

    return raddi::content::result ();
}
