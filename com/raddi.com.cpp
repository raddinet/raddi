#include <windows.h>

#include <algorithm>
#include <stdexcept>
#include <csignal>
#include <cstdarg>
#include <cwctype>
#include <memory>

#include <sodium.h>
#include <lzma.h>

#include "../common/log.h"
#include "../common/lock.h"
#include "../common/file.h"
#include "../common/options.h"
#include "../node/platform.h"
#include "../core/raddi.h"

wchar_t buffer [65536];
volatile bool quit = false;

alignas (std::uint64_t) char raddi::protocol::magic [8] = "RADDI/1";
const VS_FIXEDFILEINFO * const version = GetCurrentProcessVersionInfo ();

int argc;
wchar_t ** argw;

// TODO: add last, offline, attempt to find database for offline read access
// TODO: add support for entering data in Base64 (sodium_base642bin)

// hexadecimal
//  - parses string of two-character hexadecimal values
//    optionally separated by any non-hexadecimal character
//  - returns number of bytes written to 'content'
//
std::size_t hexadecimal (const wchar_t * p, std::uint8_t * content, std::size_t maximum) {
    std::size_t n = 0;
    std::size_t offset;
    unsigned int value;

    while (*p && maximum) {
        while (*p && !std::iswxdigit (*p)) {
            ++p;
        }
        if (std::swscanf (p, L"%2x%zn", &value, &offset) == 1) {
            p += offset;
            *content++ = value;
            --maximum;
            ++n;
        } else
            break;
    }
    return n;
}

// w2u8
//  - converts UTF-16 to UTF-8
//
std::size_t w2u8 (const wchar_t * p, std::size_t n, std::uint8_t * content, std::size_t maximum) {
    if (maximum && n)
        return WideCharToMultiByte (CP_UTF8, 0, p, (int) n, (LPSTR) content, (int) maximum, NULL, NULL);
    else
        return 0;
}

// userinput
//  - reads content as input from keyboard in the console
//  - mode specifies how many empty lines are required to terminate input
//     - no trailing LF characters are included into 'content'
//  - TODO: consider adding support for escape sequences, e.g. add newline with \n
//
std::size_t userinput (std::size_t mode, std::uint8_t * content, std::size_t maximum) {
    std::size_t nls = 0;
    std::size_t sum = 0;

    while ((nls < mode) && (maximum != 0u)) {
        std::printf ("> ");
        if (std::fgetws (buffer, sizeof buffer / sizeof buffer [0], stdin)) {

            if (std::wcslen (buffer) > 1) {
                for (; nls && maximum; --nls) {
                    *content++ = L'\n';
                    --maximum;
                    ++sum;
                }

                if (auto n = w2u8 (buffer, std::wcslen (buffer) - 1, content, maximum)) {
                    content += n;
                    maximum -= n;
                    sum += n;
                } else {
                    raddi::log::event (0x10, L"");
                }
            }
            ++nls;
        }
    }
    if (!maximum) {
        raddi::log::event (0x12);
    }
    return sum;
}

// userfile
//  - given pointer to path specification (string after '@' symbol) returns clean file path
//    and optionally sets 'offset' and 'length' to values provided after another @
//  - the syntax is: filename.ext[@offset[:length]]
//
const wchar_t * userfile (wchar_t * path, std::uintmax_t * offset, std::uintmax_t * length) {
    if (wchar_t * at = std::wcsrchr (path, L'@')) {
        wchar_t * colon = nullptr;
        if (offset) {
            *offset = std::wcstoull (at + 1, &colon, 0);
            if (length && *colon == L':') {
                *length = std::wcstoull (colon + 1, nullptr, 0);
            }
        }
        *at = L'\0';
    }
    return path;
}

// userfilecontent
//  - parses user file path (see above) and according to the parameter reads part of the file
//    into the provided content buffer
//
std::size_t userfilecontent (wchar_t * path, std::uint8_t * content, std::size_t maximum) {
    file f;
    std::uintmax_t offset = 0;
    std::uintmax_t length = (std::uintmax_t) - 1;

    if (f.open (userfile (path, &offset, &length),
                file::mode::open, file::access::read, file::share::full, file::buffer::sequential)) {

        const auto cb = f.size ();
        const auto n = std::min (std::min (cb, length), (std::uintmax_t) maximum);

        if (length != (std::uintmax_t) - 1) {
            if (offset + length > cb) {
                raddi::log::error (0x21, path, cb, offset, length, offset + length);
                return 0;
            }
        } else {
            if (offset > cb) {
                raddi::log::error (0x20, path, cb, offset);
            }
        }

        if (offset) {
            f.seek (offset);
        }
        if (f.read (content, n)) {
            return n;

        } else {
            raddi::log::error (0x1E, path);
        }
    } else {
        raddi::log::error (0x1F, path);
    }
    return 0;
}


// gather
//  - parses command-line 'text' and 'content' parameters and builds an entry content
//  - parameters are concatenated in order in which they appear on command-line
//  - returns gathered content size
//
std::size_t gather (std::uint8_t * content, std::size_t maximum) {
    const auto content_begin = content;

    // TODO: parse vote and other special tokens

    if (argc)
    for (auto i = 1uL; i != argc; ++i) {

        if (maximum == 0) {
            raddi::log::error (0x11, i);
            break;
        }

        if (std::wcsncmp (argw [i], L"text", 4) == 0) {
            if (argw [i] [4] == L':') {

                // data should almost always fit, command-line maximum is 32768 UTF-16 codepoints
                // which will fit only 16384 higher plane characters through surrogate pairs
                //  - max identity/channel announcement content lengths are less than ~65300 bytes
                
                if (auto n = w2u8 (&argw [i] [5], std::wcslen (&argw [i] [5]), content, maximum)) {
                    content += n;
                    maximum -= n;
                } else {
                    raddi::log::error (0x10, i);
                }
            }
        }
        if (std::wcsncmp (argw [i], L"content", 7) == 0) {
            if (argw [i] [7] == L':') {
                switch (argw [i] [8]) {
                    default:
                        if (auto n = hexadecimal (&argw [i] [8], content, maximum)) {
                            content += n;
                            maximum -= n;
                        }
                        break;
                    case L'*':
                        if (auto n = userinput (std::count (argw [i], argw [i] + std::wcslen (argw [i]), L'*'), content, maximum)) {
                            content += n;
                            maximum -= n;
                        }
                        break;
                    case L'@':
                        if (auto n = userfilecontent (&argw [i] [9], content, maximum)) {
                            content += n;
                            maximum -= n;
                        }
                        break;
                }
            }
        }
    }

    return content - content_begin;
}

bool identity_decode_key (const wchar_t * p, std::uint8_t (&signing_private_key) [crypto_sign_ed25519_SEEDBYTES]) {
    auto n = hexadecimal (p, signing_private_key, crypto_sign_ed25519_SEEDBYTES);
    if (n == crypto_sign_ed25519_SEEDBYTES)
        return true;
    else
        return raddi::log::error (0x14, p, crypto_sign_ed25519_SEEDBYTES, n);
}
bool identity_input_key (std::uint8_t (&signing_private_key) [crypto_sign_ed25519_SEEDBYTES]) {
    std::printf ("> ");
    if (std::fgetws (buffer, sizeof buffer / sizeof buffer [0], stdin)) {
        *std::wcsrchr (buffer, L'\n') = L'\0';
        return identity_decode_key (buffer, signing_private_key);
    }  else
        return false;
}

// identity
//  - parses command-line 'identity' parameter for identity ID and signing key
//  - required to send anything other than new:identity announcement, obviously
//  - format:
//     - identity:9f9c8f42a94a98:hexaprivatekey
//     - identity:9f9c8f42a94a98:* - read key from stdin (default)
//     - identity:9f9c8f42a94a98:@path - read key from file
//     - identity:* - read all from stdin (default), either colon or new-line (enter) separated
//
bool identity (raddi::iid & id, std::uint8_t (&signing_private_key) [crypto_sign_ed25519_SEEDBYTES]) {
    if (auto parameter = option (argc, argw, L"identity")) {

        switch (parameter [0]) {
            case L'*':
                std::printf ("> ");
                if (std::fgetws (buffer, sizeof buffer / sizeof buffer [0], stdin)) {
                    *std::wcsrchr (buffer, L'\n') = L'\0';

                    if (auto n = id.parse (buffer)) {
                        if (buffer [n] == L':')
                            return identity_decode_key (&buffer [n + 1], signing_private_key);
                        else
                            return identity_input_key (signing_private_key);
                    } else
                        return raddi::log::error (0x13, buffer);
                } else
                    return false;

            default:
                if (auto n = id.parse (parameter)) {
                    if (parameter [n] == L':') {
                        switch (parameter [n + 1]) {
                            default:
                                return identity_decode_key (&parameter [n], signing_private_key);
                            case L'*':
                                return identity_input_key (signing_private_key);
                            case L'@':
                                if (auto m = userfilecontent (const_cast <wchar_t *> (&parameter [n + 2]), // argw, this is safe
                                                              signing_private_key, sizeof signing_private_key)) {
                                    if (m == crypto_sign_ed25519_SEEDBYTES)
                                        return true;
                                    else
                                        return raddi::log::error (0x14, &parameter [n + 2], crypto_sign_ed25519_SEEDBYTES, m);
                                } else
                                    return false;
                        }
                    } else
                        return raddi::log::error (0x13, parameter);
                } else
                    return raddi::log::error (0x13, parameter);
        }
    } else
        return raddi::log::error (0x15);
}

// complexity
//  - parses and validates minimal complexity parameter
//
raddi::proof::requirements complexity (raddi::proof::requirements complexity) {
    if (auto parameter = option (argc, argw, L"complexity")) {
        do {
            auto value = std::wcstoul (parameter, (wchar_t **) &parameter, 10);
            if (value >= raddi::proof::min_complexity) {
                if (value <= raddi::proof::max_complexity) {
                    complexity.complexity = value;
                } else {
                    complexity.time = value;
                }
            }

            while (*parameter && !std::iswdigit (*parameter)) {
                ++parameter;
            }
        } while (*parameter);

        raddi::log::note (0x13, complexity.complexity, complexity.time);
    } else
        raddi::log::note (0x12, complexity.complexity, complexity.time);

    return complexity;
}

// send
//  - places data entry into instance's source directory for transmission
// 
std::size_t send (const raddi::instance & instance, const raddi::entry & entry, std::size_t size) {
    if (!instance.get <unsigned int> (L"broadcasting")) {
        raddi::log::data (0x90, instance.pid);
        SetLastError (ERROR_CONNECTION_UNAVAIL);
    }

    auto path = instance.get <std::wstring> (L"source") + entry.id.serialize ();

    file f;
    if (f.open (path, file::mode::create, file::access::write, file::share::none, file::buffer::temporary)) {
        if (f.write (&entry, size)) {
            raddi::log::event (0x91, path, size);
        } else {
            raddi::log::error (0x91, path, size);
        }
        return size;
    } else {
        return raddi::log::error (0x90, path);
    }
}

bool go ();

int wmain (int argc, wchar_t ** argw) {
    SetErrorMode (0x8007);
    SetDllDirectoryW (L"");
    RegDisablePredefinedCache ();

    std::signal (SIGBREAK, [](int) { ::quit = true; });
    std::signal (SIGTERM, [](int) { ::quit = true; });
    std::signal (SIGABRT, [](int) { ::quit = true; });
    std::signal (SIGINT, [](int) { ::quit = true; });

    sodium_init ();

    ::argc = argc;
    ::argw = argw;

#ifdef NDEBUG
    raddi::log::display (L"data");
#endif
    raddi::log::display (option (argc, argw, L"display"));
    raddi::log::initialize (option (argc, argw, L"log"), L"\\RADDI.net\\", L"cmd", false);

    raddi::log::event (0x01,
                       (unsigned long) HIWORD (version->dwProductVersionMS),
                       (unsigned long) LOWORD (version->dwProductVersionMS),
                       ARCHITECTURE, BUILD_TIMESTAMP);

    if (auto h = LoadLibrary (L"sqlite3.dll")) {
        // raddi.com does not use sqlite, but logs version number for sake of completeness
        if (auto p = reinterpret_cast <const char * (*) ()> (GetProcAddress (h, "sqlite3_libversion"))) {
            raddi::log::note (0x05, "sqlite3", p (), L"dynamic");
        }
        FreeLibrary (h);
    }
    raddi::log::note (0x05, "liblzma", lzma_version_string (), GetModuleHandle (L"liblzma") ? L"dynamic" : L"static");
    raddi::log::note (0x05, "libsodium", sodium_version_string (), GetModuleHandle (L"libsodium") ? L"dynamic" : L"static");

#ifdef CRT_STATIC
    raddi::log::note (0x05, "vcruntime", CRT_STATIC, L"static");
#else
    // NOTE: also in HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\ (debug) \ x64 | x86 \ Version
    if (auto h = GetModuleHandle (L"VCRUNTIME140")) {
        if (auto info = GetModuleVersionInfo (h)) {
            wchar_t vs [32];
            std::swprintf (vs, sizeof vs / sizeof vs [0], L"%u.%u.%u",
                           (unsigned long) HIWORD (info->dwFileVersionMS),
                           (unsigned long) LOWORD (info->dwFileVersionMS),
                           (unsigned long) HIWORD (info->dwFileVersionLS));
            raddi::log::note (0x05, "vcruntime", vs, L"dynamic");
        }
    }
#endif
    raddi::log::event (0xF0, raddi::log::path);

    SetLastError (0);
    if (go ()) {
        return ERROR_SUCCESS;
    } else {
        auto result = GetLastError ();
        if (result == ERROR_SUCCESS) {
            if (quit) {
                result = ERROR_CANCELLED;
            } else {
                result = ERROR_INVALID_PARAMETER;
            }
        }
        raddi::log::error (0xFF, raddi::log::api_error (result));
        return result;
    }
}

bool database_verification ();

bool new_identity ();
bool new_channel ();
bool reply (const wchar_t * opname, const wchar_t * to);

bool list_identities ();
bool list_channels ();

bool go () {

    // parameters settings global configuration first

    if (auto protocol = option (argc, argw, L"protocol")) {
        auto length = std::max (std::wcslen (protocol), sizeof raddi::protocol::magic);

        if (auto n = w2u8 (protocol, length, (std::uint8_t *) raddi::protocol::magic, sizeof raddi::protocol::magic)) {
            while (n != sizeof raddi::protocol::magic) {
                raddi::protocol::magic [n++] = '\0';
            }
        }
    }

    // TODO: ver/version/status (raddi::protocol::magic) ... display:all already does this
    // TODO: install / uninstall / start / stop ... (service)
    //  - ADD/REMOVE "127.0.0.1 xxx.raddi.net" to c:\Windows\System32\Drivers\etc\hosts 

    // TODO: subscribe/unsubscribe channel:eid?
    // TODO: unsubscribe all app, remove all blacklists for app
    // TODO: add/remove blacklisted thread/channel/identity

    // TODO: delete:eid database:xxx - TODO: offline or request node server to delete?
    // TODO: notify? - wait on db for an event?
    // TODO: list:instances, list:threads ...
    
    // TODO: clear blacklist, list blacklist, ban:IP?
    // TODO: clear peers (all)

    // TODO: set-log-level, set-display-level -> raddi::command



    // now/timestamp/microtimestamp
    //  - will show current time in raddi format (hexadecimal since 1.1.2018, see raddi_timestamp.h)
    //  - or converts timestamp to hierarchical datetime format: YYYY-MM-DD HH:MM:SS[.uuuuuu]

    if (option (argc, argw, L"now")) {
        std::printf ("%lx\n", raddi::now ());
        return true;
    }
    if (auto timestamp = option (argc, argw, L"microtimestamp")) {
        if (timestamp [0]) {
            wchar_t * end = nullptr;
            auto parameter = std::wcstoull (timestamp, &end, 16);
            if (*end == L'\0') {
                auto time = raddi::time ((std::uint32_t) (parameter / 1'000'000));
                std::printf ("%04d-%02d-%02d %02d:%02d:%02d.%06d\n",
                             time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
                             time.tm_hour, time.tm_min, time.tm_sec, (std::int32_t) (parameter % 1'000'000));
                return true;
            } else {
                raddi::log::error (0x1C, timestamp);
                return false;
            }
        } else {
            std::printf ("%016llx\n", raddi::microtimestamp ());
            return true;
        }
    }
    if (auto timestamp = option (argc, argw, L"timestamp")) {
        if (timestamp [0]) {
            wchar_t * end = nullptr;
            auto parameter = std::wcstoull (timestamp, &end, 16);
            if ((parameter <= 0xFFFFFFFF) && (*end == L'\0')) {
                auto time = raddi::time ((std::uint32_t) parameter);
                std::printf ("%04d-%02d-%02d %02d:%02d:%02d\n",
                             time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
                             time.tm_hour, time.tm_min, time.tm_sec);
                return true;
            } else {
                raddi::log::error (0x1C, timestamp);
                return false;
            }
        } else {
            std::printf ("%lx\n", raddi::now ());
            return true;
        }
    }

    // deepscan
    //  - this is very rough verification of the whole database

    if (option (argc, argw, L"deepscan")) {
        return database_verification ();
    }

    if (auto parameter = option (argc, argw, L"list")) {
        if (!std::wcscmp (parameter, L"instances")) {
            // TODO
        }
        if (!std::wcscmp (parameter, L"identities")) {
            return list_identities ();
        }
        if (!std::wcscmp (parameter, L"channels")) {
            return list_channels ();
        }
        // TODO: subscriptions
        // TODO: bans
        // TODO: rejected
    }

    if (auto parameter = option (argc, argw, L"new")) {
        if (!std::wcscmp (parameter, L"identity")) {
            return new_identity ();
        }
        if (!std::wcscmp (parameter, L"channel")) {
            return new_channel ();
        }
        if (!std::wcscmp (parameter, L"thread")) {
            if (auto to = option (argc, argw, L"channel")) {
                // TODO: verify that 'to' is a channel
                return reply (L"new:thread", to);
            }
        }
    }

    if (auto parameter = option (argc, argw, L"reply")) {
        return reply (L"reply", parameter);
    }

    return false;
}

template <typename T>
bool sign_and_validate (const wchar_t * step, T & entry, std::size_t size,
                        const raddi::entry & parent, std::size_t parent_size,
                        const std::uint8_t (&seed) [crypto_sign_ed25519_SEEDBYTES],
                        const std::uint8_t (&pk) [crypto_sign_ed25519_PUBLICKEYBYTES]) {
    
    std::uint8_t sk [crypto_sign_ed25519_SECRETKEYBYTES];
    std::memcpy (sk, seed, crypto_sign_ed25519_SEEDBYTES);
    std::memcpy (sk + crypto_sign_ed25519_SEEDBYTES, pk, crypto_sign_ed25519_PUBLICKEYBYTES);

    try {
        if (entry.sign (sizeof entry + size, &parent, parent_size, sk,
                        complexity (entry.default_requirements ()), &quit)) {
            if (entry.validate (&entry, sizeof entry + size)) {
                if (entry.verify (sizeof entry + size, &parent, parent_size, pk)) {
                    return true;
                } else
                    return raddi::log::stop (0x20, step, L"verify");
            } else
                return raddi::log::stop (0x20, step, L"validate");
        } else
            if (quit)
                return false;
            else
                return raddi::log::stop (0x20, step, L"sign");

    } catch (const std::bad_alloc &) {
        return raddi::log::stop (0x20, step, L"malloc");
    }
}

bool validate_identity_key (const wchar_t * step,
                            raddi::identity & identity, std::size_t size,
                            const std::uint8_t (&seed) [crypto_sign_ed25519_SEEDBYTES]) {
    bool verify = false;
    option (argc, argw, L"verify-identity-private-key", verify);

    if (verify) {
        std::uint8_t identity_signature_copy [crypto_sign_ed25519_BYTES];
        std::memcpy (identity_signature_copy, identity.signature, crypto_sign_ed25519_BYTES);
        std::memset (identity.signature, 0xCC, crypto_sign_ed25519_BYTES);

        std::uint8_t sk [crypto_sign_ed25519_SECRETKEYBYTES];
        std::memcpy (sk, seed, crypto_sign_ed25519_SEEDBYTES);
        std::memcpy (sk + crypto_sign_ed25519_SEEDBYTES, identity.public_key, crypto_sign_ed25519_PUBLICKEYBYTES);

        if (identity.sign (size, nullptr, 0, sk)) {
            return std::memcmp (identity_signature_copy, identity.signature, crypto_sign_BYTES) == 0
                || raddi::log::error (0x16);
        } else
            return raddi::log::stop (0x20, step, L"identity:sign");
    } else
        return true;
}

bool new_identity () {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    struct : public raddi::identity {
        std::uint8_t description [raddi::identity::max_description_size];
    } announcement;

    std::uint8_t private_key [crypto_sign_ed25519_SEEDBYTES];

    if (announcement.create (private_key)) {
        auto size = gather (announcement.description, sizeof announcement.description);

        if (size > raddi::consensus::max_identity_name_size) {
            return raddi::log::error (0x1D, size, raddi::consensus::max_identity_name_size);
        }

        if (sign_and_validate <raddi::identity> (L"new:identity", announcement, size, announcement, 0,
                                                 private_key, announcement.public_key)) {

            if (send (instance, announcement, sizeof (raddi::identity) + size)) {

                wchar_t string [26];
                if (announcement.id.identity.serialize (string, sizeof string / sizeof string [0])) {

                    std::printf ("%ls:", string);
                    for (auto b : private_key) std::printf ("%02x", b);
                    std::printf ("\n");

                    // TODO: support exporting private keys to file (according to parameter (export? save?)
                    // TODO: support binary output?

                    return true;
                } else
                    return raddi::log::stop (0x20, L"new:identity", L"serialize");
            }
        }
    }
    return false;
}

bool new_channel () {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    raddi::iid id;
    unsigned char key [crypto_sign_ed25519_SEEDBYTES];

    if (identity (id, key)) {

        struct : public raddi::identity {
            std::uint8_t description [raddi::identity::max_description_size];
        } parent;

        std::size_t parent_size;
        if (!database.get (id, &parent, &parent_size))
            return raddi::log::error (0x17, id.serialize ());

        if (validate_identity_key (L"new:channel", parent, parent_size, key)) {

            struct : public raddi::channel {
                std::uint8_t description [raddi::channel::max_description_size];
            } announcement;

            if (announcement.create (id)) {
                auto size = gather (announcement.description, sizeof announcement.description);

                if (size > raddi::consensus::max_channel_name_size) {
                    return raddi::log::error (0x1D, size, raddi::consensus::max_channel_name_size);
                }

                if (sign_and_validate <raddi::channel> (L"new:channel", announcement, size, parent, parent_size,
                                                        key, parent.public_key)) {
                    if (send (instance, announcement, sizeof (raddi::channel) + size)) {

                        wchar_t string [35];
                        if (announcement.id.serialize (string, sizeof string / sizeof string [0])) {
                            std::printf ("%ls\n", string);
                            return true;
                        } else
                            return raddi::log::stop (0x20, L"new:channel", L"serialize");
                    }
                }
            }
        }
    }
    return false;
}

bool reply (const wchar_t * opname, const wchar_t * to) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    raddi::iid id;
    unsigned char key [crypto_sign_ed25519_SEEDBYTES];

    if (identity (id, key)) {

        struct : public raddi::entry {
            std::uint8_t description [raddi::entry::max_content_size];
        } parent;
        struct : public raddi::identity {
            std::uint8_t description [raddi::identity::max_description_size]; // use consensus value?
        } identity;

        std::size_t identity_size;
        if (!database.get (id, &identity, &identity_size))
            return raddi::log::error (0x17, id.serialize ());

        if (!parent.id.parse (to))
            return raddi::log::error (0x18, to);

        std::size_t parent_size;
        if (!database.get (parent.id, &parent, &parent_size))
            return raddi::log::error (0x19, to);
        
        if (validate_identity_key (opname, identity, identity_size, key)) {

            struct : public raddi::entry {
                std::uint8_t description [raddi::entry::max_content_size];
            } message;

            message.id.timestamp = raddi::now ();
            message.id.identity = id;
            message.parent = parent.id;

            if (database.get (message.id, nullptr, nullptr))
                return raddi::log::error (0x1B, message.id.serialize ());

            auto size = gather (message.description, sizeof message.description);

            if (parent.is_announcement () && (size > raddi::consensus::max_thread_name_size)) {
                return raddi::log::error (0x1D, size, raddi::consensus::max_thread_name_size);
            }

            if (sign_and_validate <raddi::entry> (opname, message, size, parent, parent_size, key, identity.public_key)) {
                if (send (instance, message, sizeof (raddi::entry) + size)) {

                    wchar_t string [35];
                    if (message.id.serialize (string, sizeof string / sizeof string [0])) {
                        std::printf ("%ls\n", string);
                        return true;
                    } else
                        return raddi::log::stop (0x20, opname, L"serialize");
                }
            }
        }
    }
    return false;
}

struct list_column_info {
    int          width;
    const char * format;
    const char * name;
};

template <typename T>
bool list_core_table (T * table, list_column_info (&columns) [6]) {
    std::uint32_t oldest = 0;
    std::uint32_t latest = raddi::now ();

    if (auto p = option (argc, argw, L"oldest")) {
        oldest = std::wcstoul (p, nullptr, 16);
    }
    if (auto p = option (argc, argw, L"latest")) {
        latest = std::wcstoul (p, nullptr, 16);
    }
    for (auto column : columns) {
        std::printf ("%*s", column.width, column.name);
    }
    std::printf ("\n");

    table->select (oldest, latest,
                   [] (const auto &, const auto &) { return true; },
                   [&columns] (const auto & row, const auto & detail) {
                        std::printf (columns [0].format, columns [0].width, row.id.serialize ().c_str ());
                        std::printf (columns [1].format, columns [1].width, detail.shard);
                        std::printf (columns [2].format, columns [2].width, detail.index + 1);
                        std::printf (columns [3].format, columns [3].width, detail.count);
                        std::printf (columns [4].format, columns [4].width, row.data.offset);
                        std::printf (columns [5].format, columns [5].width, row.data.length + sizeof (raddi::entry::signature) + sizeof (raddi::entry::proof));
                        std::printf ("\n");
                        return false;
                   },
                   [] (const auto &, const auto &, std::uint8_t *) { /* noop */});
    return true;
}

bool list_identities () {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    // TODO: optionally display name

    list_column_info columns [] = {
        { 16, "%*ls", "iid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*llu", "n" },
        { 9, "%*u", "offset" },
        { 4, "%*u", "size" },
    };
    return list_core_table (database.identities.get (), columns);

}

bool list_channels () {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    // TODO: restrict by identity (creator)
    // TODO: optionally display title

    list_column_info columns [] = {
        { 24, "%*ls", "eid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*llu", "n" },
        { 9, "%*u", "offset" },
        { 4, "%*u", "size" },
    };
    return list_core_table (database.channels.get (), columns);
}


bool database_verification () {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    struct : public raddi::entry {
        std::uint8_t description [raddi::entry::max_content_size];
    } parent;
    struct : public raddi::entry {
        std::uint8_t description [raddi::entry::max_content_size];
    } suspect;
    struct : public raddi::identity {
        std::uint8_t description [raddi::identity::max_description_size];
    } identity;

    // validate identities
    //  - TODO: improve reporting, use raddi::log

    database.identities->select (
        0, raddi::now (),
        [&database, &identity] (const auto & row, const auto & detail) {
            printf ("IDENTITY %ls: ", row.id.serialize ().c_str ());

            std::size_t identity_size;
            if (database.get (row.id, &identity, &identity_size)) {

                if (identity.validate (&identity, identity_size)) {
                    if (identity.verify (identity_size, nullptr, 0, identity.public_key)) {
                        printf ("OK\n");
                    } else {
                        printf ("DOES NOT VERIFY\n");
                    }
                } else {
                    printf ("DOES NOT VALIDATE\n");
                }
            } else
                raddi::log::error (0x17, identity.id.serialize ());

            return false;
        },
        [] (const auto &, const auto & detail) { return false; },
        [] (const auto &, const auto & detail, std::uint8_t *) {});

    // validate channels

    database.channels->select (
        0, raddi::now (),
        [&database, &identity, &suspect] (const auto & row, const auto & detail) {
            printf ("CHANNEL %ls: ", row.id.serialize ().c_str ());

            std::size_t identity_size;
            std::size_t suspect_size;
            if (!database.get (row.id.identity, &identity, &identity_size)) {
                printf ("IDENTITY %ls DOES NOT EXIST\n", row.id.identity.serialize ().c_str ());
                return false;
            }
            if (!database.get (row.id, &suspect, &suspect_size)) {
                printf ("DOES NOT EXIST\n");
                return false;
            }

            if (suspect.validate (&suspect, suspect_size)) {
                if (suspect.verify (suspect_size, &identity, identity_size, identity.public_key)) {
                    printf ("OK\n");
                } else {
                    printf ("DOES NOT VERIFY\n");
                }
            } else {
                printf ("DOES NOT VALIDATE\n");
            }

            return false;
        },
        [] (const auto &, const auto & detail) { return false; },
        [] (const auto &, const auto & detail, std::uint8_t *) {});

    // validate threads

    database.threads->select (
        0, raddi::now (),
        [&database, &identity, &parent, &suspect] (const auto & row, const auto & detail) {
            printf ("THREAD %ls: ", row.id.serialize ().c_str ());

            std::size_t identity_size;
            std::size_t suspect_size;
            std::size_t parent_size;
            if (!database.get (row.id.identity, &identity, &identity_size)) {
                printf ("IDENTITY %ls DOES NOT EXIST\n", row.id.identity.serialize ().c_str ());
                return false;
            }
            if (!database.get (row.parent, &parent, &parent_size)) {
                printf ("PARENT DOES NOT EXIST\n");
                return false;
            }
            if (!database.get (row.id, &suspect, &suspect_size)) {
                printf ("DOES NOT EXIST\n");
                return false;
            }

            if (suspect.validate (&suspect, suspect_size)) {
                if (suspect.verify (suspect_size, &parent, parent_size, identity.public_key)) {
                    printf ("OK\n");
                } else {
                    printf ("DOES NOT VERIFY\n");
                }
            } else {
                printf ("DOES NOT VALIDATE\n");
            }

            return false;
        },
        [] (const auto &, const auto & detail) { return false; },
        [] (const auto &, const auto & detail, std::uint8_t *) {});

    // validate data
        
    database.data->select (
        0, raddi::now (),
        [&database, &identity, &parent, &suspect] (const auto & row, const auto & detail) {
            printf ("ENTRY %ls: ", row.id.serialize ().c_str ());

            std::size_t identity_size;
            std::size_t suspect_size;
            std::size_t parent_size;
            if (!database.get (row.id.identity, &identity, &identity_size)) {
                printf ("IDENTITY %ls DOES NOT EXIST\n", row.id.identity.serialize ().c_str ());
                return false;
            }
            if (!database.get (row.parent, &parent, &parent_size)) {
                printf ("PARENT DOES NOT EXIST\n");
                return false;
            }
            if (!database.get (row.id, &suspect, &suspect_size)) {
                printf ("DOES NOT EXIST\n");
                return false;
            }

            if (suspect.validate (&suspect, suspect_size)) {
                if (suspect.verify (suspect_size, &parent, parent_size, identity.public_key)) {
                    printf ("OK\n");
                } else {
                    printf ("DOES NOT VERIFY\n");
                }
            } else {
                printf ("DOES NOT VALIDATE\n");
            }

            return false;
        },
        [] (const auto &, const auto & detail) { return false; },
        [] (const auto &, const auto & detail, std::uint8_t *) {});

    return true;
}

// this needs to be implemented by threadpool of any serious client app
// to keep track of changes etc. but raddi.com does one thing and exits
// so no threadpool is actually necessary

bool Overlapped::await (HANDLE handle, void * key) noexcept { return true; }
bool Overlapped::enqueue () noexcept { return true; }

#ifdef CRT_STATIC
extern "C" char * __cdecl __unDName (void *, const void *, int, void *, void *, unsigned short) {
    return nullptr;
}
extern "C" LCID __cdecl __acrt_DownlevelLocaleNameToLCID (LPCWSTR) {
    return 0;
}
extern "C" int __cdecl __acrt_DownlevelLCIDToLocaleName (LCID, LPWSTR, int) {
    return 0;
}
#endif
