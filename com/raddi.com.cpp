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
#include "../common/platform.h"

#include "../core/raddi.h"
#include "../core/raddi_request.h"
#include "../core/raddi_content.h"
#include "../core/raddi_defaults.h"

uuid app;
wchar_t buffer [4*65536];
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
std::size_t hexadecimal (const char * p, std::uint8_t * content, std::size_t maximum) {
    std::size_t n = 0;
    std::size_t offset;
    unsigned int value;

    while (*p && maximum) {
        while (*p && !std::iswdigit (*p)) {
            ++p;
        }
        if (std::sscanf (p, "%2x%zn", &value, &offset) == 1) {
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

// u82ws
//  - converts UTF-8 to UTF-16 (wchar_t) string
//
std::wstring u82ws (const uint8_t * data, std::size_t size) {
    std::wstring result;
    if (data && size) {
        if (auto n = MultiByteToWideChar (CP_UTF8, 0, (LPCCH) data, (int) size, NULL, 0)) {
            result.resize (n);
            MultiByteToWideChar (CP_UTF8, 0, (LPCCH) data, (int) size, &result [0], n);
        };
    }
    return result;
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
        if (f.read (content, (std::size_t) n)) {
            return (std::size_t) n;

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

// send
//  - places command into instance's source directory for transmission
// 
std::size_t send (const raddi::instance & instance, enum class raddi::command::type cmd) {
    if (!instance.get <unsigned int> (L"broadcasting")) {
        raddi::log::data (0x90, instance.pid);
        SetLastError (ERROR_CONNECTION_UNAVAIL);
    }

    auto path = instance.get <std::wstring> (L"source") + std::to_wstring (raddi::microtimestamp ());

    file f;
    if (f.open (path, file::mode::create, file::access::write, file::share::none, file::buffer::temporary)) {
        if (f.write (&cmd, sizeof cmd)) {
            raddi::log::event (0x91, path, sizeof cmd);
        } else {
            raddi::log::error (0x91, path, sizeof cmd);
        }
        return sizeof cmd;
    } else {
        return raddi::log::error (0x90, path);
    }
}

// send
//  - places command and data into instance's source directory for transmission
// 
template <typename T>
std::size_t send (const raddi::instance & instance, enum class raddi::command::type cmd, const T & payload) {
    if (!instance.get <unsigned int> (L"broadcasting")) {
        raddi::log::data (0x90, instance.pid);
        SetLastError (ERROR_CONNECTION_UNAVAIL);
    }

    auto path = instance.get <std::wstring> (L"source") + std::to_wstring (raddi::microtimestamp ());

    file f;
    if (f.open (path, file::mode::create, file::access::write, file::share::none, file::buffer::temporary)) {
        if (f.write (&cmd, sizeof cmd)) {
            if (f.write (&payload, sizeof payload)) {
                raddi::log::event (0x91, path, sizeof cmd + sizeof payload);
            } else {
                raddi::log::error (0x91, path, sizeof payload);
            }
        } else {
            raddi::log::error (0x91, path, sizeof cmd);
        }
        return sizeof cmd + sizeof payload;
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

    // apps need their UUID for daemon to distinguish them:
    //  - either hard-code the UUID, every app different!
    //  - or generate on first run and store in app's settings, like this:

    HKEY registry;
    if (RegCreateKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\RADDI.net", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &registry, NULL) == ERROR_SUCCESS) {
        DWORD size = sizeof app;
        if (RegQueryValueEx (registry, L"raddi.com", NULL, NULL, (LPBYTE) &app, &size) != ERROR_SUCCESS) {
            RegSetValueEx (registry, L"raddi.com", 0, REG_BINARY, (LPBYTE) &app, sizeof app);
        }
        RegCloseKey (registry);
        registry = NULL;
    }

    WSADATA wsa;
    WSAStartup (0x0202, &wsa);

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

bool benchmark ();
bool database_verification ();

bool new_identity ();
bool new_channel ();
bool reply (const wchar_t * opname, const wchar_t * to);

bool list_identities ();
bool list_channels ();
bool list_threads ();
bool get (const wchar_t * eid);
bool analyze (const std::uint8_t * data, std::size_t size);

bool download_command (const wchar_t * what);
bool erase_command (const wchar_t * what);
bool peer_command (enum class raddi::command::type, const wchar_t * address);
bool subscription_command (enum class raddi::command::type, const wchar_t * eid);

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

    // TODO: install / uninstall / start / stop ... (service)
    //  - ADD/REMOVE "127.0.0.1 xxx.raddi.net" to c:\Windows\System32\Drivers\etc\hosts 

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

    // benchmark
    //  - measures how long it takes to find predetermined solution

    if (option (argc, argw, L"benchmark")) {
        return benchmark ();
    }

    if (auto parameter = option (argc, argw, L"list")) {
        /*if (!std::wcscmp (parameter, L"instances")) {
            // TODO
        }*/
        if (!std::wcscmp (parameter, L"identities")) {
            return list_identities ();
        }
        if (!std::wcscmp (parameter, L"channels")) {
            return list_channels ();
        }
        if (!std::wcscmp (parameter, L"threads")) {
            return list_threads ();
        }
        // TODO: bans
        // TODO: rejected
        // TODO: subscriptions/blacklisted/retained
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

    if (auto parameter = option (argc, argw, L"get")) {
        return get (parameter);
    }
    if (auto parameter = option (argc, argw, L"analyze")) {
        std::size_t size = 0;
        std::uint8_t data [raddi::entry::max_content_size];

        switch (parameter [0]) {
            case L'*':
                size = userinput (std::count (parameter, parameter + std::wcslen (parameter), L'*'), data, sizeof data);
                data [size] = '\0';
                size = hexadecimal (reinterpret_cast <char *> (data), data, sizeof data);
                break;
            case L'@':
                size = userfilecontent (const_cast <wchar_t *> (&parameter [1]), data, sizeof data);
                break;
            default:
                size = hexadecimal (parameter, data, sizeof data);
                break;
        }
        if (size)
            return analyze (data, size);
        else
            return false;
    }

    // TODO: document all below

    if (auto parameter = option (argc, argw, L"echo")) {
        if (!std::wcscmp (parameter, L"connection")) {

            raddi::instance instance (option (argc, argw, L"instance"));
            if (instance.status != ERROR_SUCCESS)
                return raddi::log::data (0x91);
            else
                return send (instance, raddi::command::type::log_conn_status);
        }
    }

    if (auto parameter = option (argc, argw, L"add")) {
        bool core = false;
        option (argc, argw, L"core", core);
        return peer_command (core ? raddi::command::type::add_core_peer : raddi::command::type::add_peer, parameter);
    }
    if (auto parameter = option (argc, argw, L"remove")) {
        return peer_command (raddi::command::type::rem_peer, parameter);
    }
    if (auto parameter = option (argc, argw, L"ban")) {
        return peer_command (raddi::command::type::ban_peer, parameter);
    }
    if (auto parameter = option (argc, argw, L"unban")) {
        return peer_command (raddi::command::type::unban_peer, parameter);
    }
    if (auto parameter = option (argc, argw, L"connect")) {
        return peer_command (raddi::command::type::connect_peer, parameter);
    }

    if (auto parameter = option (argc, argw, L"download")) {
        return download_command (parameter);
    }
    if (auto parameter = option (argc, argw, L"erase")) {
        return erase_command (parameter);
    }

    if (auto parameter = option (argc, argw, L"subscribe")) {
        return subscription_command (raddi::command::type::subscribe, parameter);
    }
    if (auto parameter = option (argc, argw, L"unsubscribe")) {
        return subscription_command (raddi::command::type::unsubscribe, parameter);
    }
    if (auto parameter = option (argc, argw, L"blacklist")) {
        return subscription_command (raddi::command::type::blacklist, parameter);
    }
    if (auto parameter = option (argc, argw, L"unblacklist")) { // TODO: better name?
        return subscription_command (raddi::command::type::unblacklist, parameter);
    }
    if (auto parameter = option (argc, argw, L"retain")) {
        return subscription_command (raddi::command::type::retain, parameter);
    }
    if (auto parameter = option (argc, argw, L"unretain")) { // TODO: better name?
        return subscription_command (raddi::command::type::unretain, parameter);
    }

    /*if (auto parameter = option (argc, argw, L"test")) {
        if (!std::wcscmp (parameter, L"content")) {

            std::uint8_t buffer [65536];
            while (!quit) {
                const auto length = std::rand () % sizeof buffer;
                randombytes_buf (buffer, length);

                std::printf ("test [%5u]: %16llX\n", length, raddi::content::analyze (buffer, length).summarize ().raw);
            }
        }
    }// */

    return false;
}

bool StringToAddress (SOCKADDR_INET & address, const wchar_t * string) noexcept {
    std::memset (&address, 0, sizeof address);
    INT length = sizeof address;
    return WSAStringToAddress (const_cast <wchar_t *> (string), AF_INET6, NULL, reinterpret_cast <SOCKADDR *> (&address.Ipv6), &length) == 0
        || WSAStringToAddress (const_cast <wchar_t *> (string), AF_INET, NULL, reinterpret_cast <SOCKADDR *> (&address.Ipv4), &length) == 0;
}

bool peer_command (enum class raddi::command::type cmd, const wchar_t * addr) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    SOCKADDR_INET sa;
    if (!StringToAddress (sa, addr))
        return raddi::log::data (0x93, addr);

    switch (cmd) {
        case raddi::command::type::add_peer:
        case raddi::command::type::add_core_peer:
        case raddi::command::type::connect_peer:
            switch (sa.si_family) {
                case AF_INET:
                    if (sa.Ipv4.sin_port == 0) {
                        sa.Ipv4.sin_port = htons (raddi::defaults::coordinator_listening_port);
                    }
                    break;
                case AF_INET6:
                    if (sa.Ipv6.sin6_port == 0) {
                        sa.Ipv6.sin6_port = htons (raddi::defaults::coordinator_listening_port);
                    }
                    break;
            }
            break;

        case raddi::command::type::ban_peer:
            // TODO: days
            break;
    }

    return send (instance, cmd, raddi::address (sa));
}

bool subscription_command (enum class raddi::command::type cmd, const wchar_t * eid) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::eid channel;
    if (!channel.parse (eid))
        return raddi::log::data (0x92, eid);

    option (argc, argw, L"app", app);

    return send (instance, cmd, raddi::command::subscription { channel, app });
}

bool download_command (const wchar_t * what) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::eid channel;
    if (std::wcscmp (what, L"all") == 0) {
        channel.timestamp = 0;
        channel.identity.timestamp = 0;
        channel.identity.nonce = 0;
    } else
        if (!channel.parse (what))
            return raddi::log::data (0x92, what);

    std::uint32_t threshold = raddi::now () - 31 * 86400;
    option (argc, argw, L"threshold", threshold);

    return send (instance, raddi::command::type::download, raddi::request::download { channel, threshold });
}

bool erase_command (const wchar_t * what) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::eid entry;
    if (!entry.parse (what))
        return raddi::log::data (0x92, what);

    bool quick = false;
    option (argc, argw, L"quick", quick);

    return send (instance, quick ? raddi::command::type::erase : raddi::command::type::erase_thorough);
}

template <typename T>
std::size_t sign_and_validate (const wchar_t * step, T & entry, std::size_t size,
                               const std::uint8_t (&seed) [crypto_sign_ed25519_SEEDBYTES],
                               const std::uint8_t (&pk) [crypto_sign_ed25519_PUBLICKEYBYTES]) {
    
    std::uint8_t sk [crypto_sign_ed25519_SECRETKEYBYTES];
    std::memcpy (sk, seed, crypto_sign_ed25519_SEEDBYTES);
    std::memcpy (sk + crypto_sign_ed25519_SEEDBYTES, pk, crypto_sign_ed25519_PUBLICKEYBYTES);

    try {
        if (auto a = entry.sign (sizeof entry + size, sk,
                                 complexity (entry.default_requirements ()), &quit)) {
            size += a;
            if (entry.validate (&entry, sizeof entry + size)) {
                if (entry.verify (sizeof entry + size, pk)) {
                    return size;
                } else
                    return raddi::log::stop (0x20, step, L"verify");
            } else
                return raddi::log::stop (0x20, step, L"validate");
        } else
            return 0;

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

        if (identity.sign (size, sk)) {
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

    auto description_size = gather (announcement.description, sizeof announcement.description);
    if (description_size > raddi::consensus::max_identity_name_size) {
        return raddi::log::error (0x1D, description_size, raddi::consensus::max_identity_name_size);
    }

    while (!quit) {
        std::uint8_t private_key [crypto_sign_ed25519_SEEDBYTES];
        if (announcement.create (private_key)) {

            if (auto size = sign_and_validate <raddi::identity> (L"new:identity", announcement, description_size,
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

            auto description_size = gather (announcement.description, sizeof announcement.description);
            if (description_size > raddi::consensus::max_channel_name_size) {
                return raddi::log::error (0x1D, description_size, raddi::consensus::max_channel_name_size);
            }

            while (!quit) {
                if (announcement.create (id)) {
                    if (auto size = sign_and_validate <raddi::channel> (L"new:channel", announcement, description_size,
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

        struct : public raddi::identity {
            std::uint8_t description [raddi::identity::max_description_size];
        } identity;

        std::size_t identity_size;
        if (!database.get (id, &identity, &identity_size))
            return raddi::log::error (0x17, id.serialize ());

        struct : public raddi::entry {
            std::uint8_t description [raddi::entry::max_content_size];
        } message;

        if (!message.parent.parse (to))
            return raddi::log::error (0x18, to);

        // TODO: if option "append-parent-hash"
        //  - fetch parent, hash, append

        if (validate_identity_key (opname, identity, identity_size, key)) {

            if (database.get (message.id, nullptr, nullptr))
                return raddi::log::error (0x1B, message.id.serialize ());

            auto description_size = gather (message.description, sizeof message.description);

            while (!quit) {
                message.id.timestamp = raddi::now ();
                message.id.identity = id;
                 
                if (auto size = sign_and_validate <raddi::entry> (opname, message, description_size, key, identity.public_key)) {
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
    }
    return false;
}

struct list_column_info {
    int          width;
    const char * format;
    const char * name;
};

template <typename T, typename Constrain>
bool list_core_table (T * table, list_column_info (&columns) [7], std::size_t skip, Constrain constrain) {
    std::uint32_t oldest = 0;
    std::uint32_t latest = raddi::now ();
    
    bool names = true;
    option (argc, argw, L"names", names);

    if (auto p = option (argc, argw, L"oldest")) {
        oldest = std::wcstoul (p, nullptr, 16);
    }
    if (auto p = option (argc, argw, L"latest")) {
        latest = std::wcstoul (p, nullptr, 16);
    }
    for (auto column : columns) {
        if (std::strchr (column.format, '-')) {
            std::printf (" %-*s", column.width, column.name);
        } else {
            std::printf ("%*s", column.width, column.name);
        }
    }
    std::printf ("\n");

    table->select (oldest, latest,
                   constrain,
                   [&columns, names] (const auto & row, const auto & detail) {
                        std::printf (columns [0].format, columns [0].width, row.id.serialize ().c_str ());
                        std::printf (columns [1].format, columns [1].width, detail.shard);
                        std::printf (columns [2].format, columns [2].width, detail.index + 1);
                        std::printf (columns [3].format, columns [3].width, detail.count);
                        std::printf (columns [4].format, columns [4].width, row.data.offset);
                        std::printf (columns [5].format, columns [5].width, row.data.length + sizeof (raddi::entry::signature));
                        if (names) {
                            return true;
                        } else {
                            std::printf ("\n");
                            return false;
                        }
                   },
                   [&columns, skip] (const auto & row, const auto &, std::uint8_t * raw) {
                        const auto entry = reinterpret_cast <raddi::entry *> (raw);

                        std::size_t proof_size = 0;
                        if (entry->proof (row.data.length + sizeof (raddi::entry), &proof_size)) {

                            auto analysis = raddi::content::analyze (entry->content () + skip, row.data.length - proof_size - skip);
                            std::wstring name;

                            for (const auto & text : analysis.text) {
                                if (text.paragraph)
                                    break;

                                name += u82ws (text.begin, (std::size_t) (text.end - text.begin));
                            }

                            if (name.length () > (unsigned) columns [6].width) {
                                name.resize (columns [6].width - 3);
                                name += L"...";
                            }

                            std::wprintf (L" %s\n", name.c_str ());
                        } else
                            raddi::log::stop (0x21, row.id);
                   });
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

    list_column_info columns [] = {
        { 16, "%*ls", "iid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*llu", "n" },
        { 9, "%*u", "offset" },
        { 5, "%*u", "size" },
        { 30, "%-*ls", "name" }, // raddi::consensus::max_identity_name_size
    };
    return list_core_table (database.identities.get (), columns,
                            sizeof (raddi::identity) - sizeof (raddi::entry),
                            [] (const auto &, const auto &) { return true; });
}

bool list_channels () {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    list_column_info columns [] = {
        { 24, "%*ls", "eid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*llu", "n" },
        { 9, "%*u", "offset" },
        { 5, "%*u", "size" },
        { 22, "%-*ls", "name" }, // raddi::consensus::max_channel_name_size
    };

    if (auto iid = option (argc, argw, L"author")) {

        raddi::iid author;
        if (!author.parse (iid))
            return raddi::log::data (0x95, iid);

        if (!database.identities->get (author))
            return raddi::log::data (0x96, author);

        return list_core_table (database.channels.get (), columns,
                                sizeof (raddi::channel) - sizeof (raddi::entry),
                                [author] (const auto & row, const auto &) { return row.id.identity == author; });
    } else {
        return list_core_table (database.channels.get (), columns,
                                sizeof (raddi::channel) - sizeof (raddi::entry),
                                [] (const auto &, const auto &) { return true; });
    }
}

bool list_threads () {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    raddi::eid channel;
    if (auto eid = option (argc, argw, L"channel")) {
        if (!channel.parse (eid))
            return raddi::log::data (0x92, eid);

        if (channel.timestamp == channel.identity.timestamp) {
            // identity channel
            if (!database.identities->get (channel.identity))
                return raddi::log::data (0x95, channel.identity);
        } else {
            // normal channel
            if (!database.channels->get (channel))
                return raddi::log::data (0x94, channel);
        }
    } else
        return raddi::log::error (0x22);

    list_column_info columns [] = {
        { 24, "%*ls", "eid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*llu", "n" },
        { 9, "%*u", "offset" },
        { 5, "%*u", "size" },
        { 22, "%-*ls", "name" }, // raddi::consensus::max_thread_name_size
    };

    if (auto iid = option (argc, argw, L"author")) {

        raddi::iid author;
        if (!author.parse (iid))
            return raddi::log::data (0x95, iid);

        if (!database.identities->get (author))
            return raddi::log::data (0x96, author);

        return list_core_table (database.threads.get (), columns, 0,
                                [channel, author] (const auto & row, const auto &) {
                                    return channel == row.top ().channel
                                        && author == row.id.identity;
                                });
    } else {
        return list_core_table (database.threads.get (), columns, 0,
                                [channel] (const auto & row, const auto &) {
                                    return channel == row.top ().channel;
                                });
    }
}

bool analyze (const std::uint8_t * data, std::size_t size) {
    
    // TODO: better format

    auto analysis = raddi::content::analyze (data, size);

    if (!analysis.markers.empty ()) {
        std::printf ("MARKERS:\n\t");
        for (const auto & marker : analysis.markers) {
            switch (marker.type) {
                // case 0x03: std::printf (" ETX"); break;
                // case 0x04: std::printf (" EOT"); break;
                // case 0x05: std::printf (" ENQ"); break;
                case 0x06: std::printf (" ACK"); break;
                case 0x07: std::printf (" REPORT"); break;
                case 0x08: std::printf (" REVERT"); break;
                // case 0x0C: std::printf (" FF"); break;
                // case 0x17: std::printf (" ETB"); break;
                // case 0x19: std::printf (" EM"); break;
                case 0x1C: std::printf (" FS"); break; // FS, table  --.
                case 0x1D: std::printf (" GS"); break; // GS, tab (?)--+-- used to render tables
                case 0x1E: std::printf (" RS"); break; // RS, row    --|
                case 0x1F: std::printf (" US"); break; // US, column --'
                case 0x7F: std::printf (" DELETE"); break;
                default:
                    std::printf (" %u", marker.type);
            }
            if (marker.insertion) {
                std::printf (" @%u", marker.insertion);
                // TODO: remember and display symbol in text, different color
            }
        }
        std::printf ("\n");
    }
    if (!analysis.tokens.empty ()) {
        std::printf ("TOKENS:\n");
        for (const auto & token : analysis.tokens) {
            switch (token.code) {
                case 0x0B: std::printf ("\tVOTE: "); break;
                case 0x10: std::printf ("\tSIDEBAND: "); break;
                case 0x15: std::printf ("\tMODERATION: "); break;
                // case 0x1A: std::printf ("\tSUB: "); break;
                default:
                    std::printf ("\t%u: ", token.type);
            }
            if (token.code || !token.string) {
                std::printf ("0x%02x", token.code);
            }
            if (token.string) {
                std::wprintf (L"%s", u82ws (token.string, token.string_end - token.string).c_str ());
            }
            if (token.insertion) {
                std::printf (" @%u", token.insertion);
                // TODO: remember and display symbol in text, different color
            }
            if (token.truncated) {
                std::printf (" TRUNCATED!");
            }
            std::printf ("\n");
        }
    }
    if (!analysis.attachments.empty ()) {
        std::printf ("ATTACHMENTS:\n");
        for (const auto & attachment : analysis.attachments) {
            switch (attachment.type) {
                case 0xFA: std::printf ("\tBINARY ATTACHMENT: "); break;
                case 0xFC: std::printf ("\tCOMPRESSED DATA: "); break;
                case 0xFE: std::printf ("\tENCRYPTED DATA: "); break;
                case 0xFF: std::printf ("\tPRIVATE MESSAGE: "); break;
                default:
                    std::printf ("\t%u: ", attachment.type);
            }
            std::printf ("%u bytes", attachment.size);

            if (attachment.insertion) {
                std::printf (" @%u", attachment.insertion);
                // TODO: remember and display symbol in text, different color
            }
            if (attachment.truncated) {
                std::printf (" TRUNCATED!");
            }
            if (attachment.type == 0xFC) {
                // TODO: decompress and display
            }
            std::printf ("\n");
        }
    }
    for (const auto & edit : analysis.edits) {
        std::printf ("EDIT: offset %u, %u bytes, replace with following %u bytes%s:\n",
                     edit.offset, edit.length, edit.string_end - edit.string, edit.truncated ? " (truncated)" : "");
        analyze (edit.string, edit.string_end - edit.string);
        std::printf ("\n");
    }
    if (!analysis.text.empty ()) {
        std::printf ("TEXT:\n");

        for (const auto & text : analysis.text) {
            if (text.paragraph) {
                std::printf ("\n\n");
            }
            if (text.heading) {
                // TODO: set bg color
            } else {
                // TODO: normal bg color
            }
            std::wprintf (L"%s", u82ws (text.begin, (std::size_t) (text.end - text.begin)).c_str ());
        }
    }
    return true;
}

bool get (const wchar_t * what) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read,
                        instance.get <std::wstring> (L"database").c_str ());
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    raddi::eid eid;
    if (!eid.parse (what))
        return raddi::log::data (0x92, what);

    std::size_t size = 0;
    std::uint8_t data [raddi::protocol::max_payload];

    if (database.get (eid, data, &size)) {
        const auto entry = reinterpret_cast <raddi::entry *> (data);

        std::size_t proof_size = 0;
        if (entry->proof (size, &proof_size)) {
            
            int skip = 0;
            switch (entry->is_announcement ()) {
                case raddi::entry::new_identity_announcement:
                    skip = sizeof (raddi::identity) - sizeof (raddi::entry);
                    break;
                case raddi::entry::new_channel_announcement:
                    skip = sizeof (raddi::channel) - sizeof (raddi::entry);
                    break;
                case raddi::entry::not_an_announcement:
                    break;
            }

            return analyze (entry->content () + skip, size - proof_size - skip);
        } else {
            return raddi::log::stop (0x21, eid);
        }
    } else
        return raddi::log::error (0x19, eid);
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
                    if (identity.verify (identity_size, identity.public_key)) {
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
                if (suspect.verify (suspect_size, identity.public_key)) {
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
                if (suspect.verify (suspect_size, identity.public_key)) {
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
                if (suspect.verify (suspect_size, identity.public_key)) {
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

bool benchmark () {
    std::uint8_t hash [crypto_hash_sha512_BYTES];
    std::uint8_t buffer [raddi::proof::max_size];
    
    static const std::uint8_t expected [] [raddi::proof::max_size] = {
        {
            0x00,
            0x43,0xa0,0x32,0x00, 0xe0,0x6e,0x04,0x00, 0x14,0x2c,0x27,0x00, 0xac,0xf2,0x20,0x00,
            0x6c,0xbe,0x03,0x00, 0x37,0x3d,0x3f,0x00, 0xab,0x78,0x4e,0x01, 0x97,0x55,0x66,0x00,
            0x0a,0x73,0x1e,0x00, 0x16,0xd9,0x1c,0x00, 0xc3,0x95,0x5c,0x00, 0x6b,0x5e,0x03,0x00,
            0x82,0xd1,0x37,0x00, 0x86,0x7f,0x86,0x00,
            0x81
        }, {
            0x00,
            0xb1,0x42,0x1e,0x00, 0xe1,0xea,0x31,0x00, 0xff,0x76,0x3e,0x00, 0x9a,0x28,0x5b,0x00,
            0x2b,0x29,0x47,0x00, 0x7a,0xfa,0x49,0x00, 0x14,0x51,0x3b,0x00, 0x46,0xe4,0x3e,0x01,
            0xbb,0xd7,0x47,0x01, 0x00,0xaf,0x2b,0x00, 0xf7,0x24,0x25,0x00, 0xf8,0x15,0x60,0x00,
            0x5d,0xfc,0x7a,0x01, 0xf0,0x8f,0x14,0x00, 0x0f,0xe5,0xac,0x00, 0x69,0x27,0x01,0x00,
            0x8b,0x48,0x0a,0x00, 0x71,0x55,0xc5,0x00,
            0x93
        }, {
            0x00,
            0x3e,0x38,0x3b,0x00, 0xd8,0x49,0xbe,0x00, 0x7f,0x62,0x16,0x00, 0xa2,0x0e,0xb9,0x00,
            0x4d,0x0f,0x76,0x01, 0x5c,0x95,0x0e,0x01, 0x0a,0x8f,0xce,0x00, 0x8e,0x0f,0x29,0x01,
            0x25,0xbf,0x1f,0x00, 0x6e,0xd7,0x07,0x00, 0x62,0x14,0xf4,0x00, 0xeb,0xdb,0xe6,0x00,
            0xd2,0xb6,0x3c,0x01, 0xe7,0xf4,0xbc,0x01, 0xa7,0xb6,0xbd,0x00, 0x20,0x83,0x19,0x00,
            0x38,0xa6,0x8a,0x01, 0xdb,0x13,0x46,0x00, 0xdb,0xc6,0x40,0x01, 0x94,0x34,0x3b,0x00,
            0xa4,
        }, {
            0x00,
            0xb6,0x6f,0x84,0x00, 0x2d,0x29,0x20,0x01, 0xa6,0x7c,0xb8,0x03, 0x8e,0x96,0xfd,0x00,
            0x0f,0x14,0x38,0x06, 0x65,0x22,0x4c,0x02, 0x29,0x03,0x33,0x00, 0x06,0x38,0x79,0x01,
            0x62,0x69,0xa5,0x01, 0xdd,0x72,0xbc,0x00, 0xbc,0xf5,0xc0,0x03, 0xd0,0xee,0x6f,0x02,
            0xb5,0x7b,0x30,0x00, 0xe6,0x8b,0xdd,0x02, 0x0d,0x4e,0x11,0x00, 0xdf,0x3b,0x14,0x00,
            0x35,0x5a,0x45,0x00, 0x8b,0x5b,0xfc,0x01,
            0xb3,
        }
    };

    for (auto i = 0u; i != sizeof hash; ++i) {
        hash [i] = (i << 2) ^ i ;
    }
    for (auto complexity = raddi::proof::min_complexity; (complexity != raddi::proof::max_complexity + 1) && !quit; ++complexity) {
        printf ("benchmarking complexity %u: ", complexity);
        try {
            auto t0 = raddi::microtimestamp ();
            if (auto n = raddi::proof::generate (hash, buffer, sizeof buffer, { complexity, 0 }, &quit)) {
                printf ("found... %.1fs\n", (raddi::microtimestamp () - t0) / 1000000.0);
                for (auto i = 0u; i != n; ++i) {
                    if (buffer [i] != expected [complexity - raddi::proof::min_complexity] [i]) {
                        printf ("unexpected validation failure\n");
                        return false;
                    }
                }
            } else {
                printf ("unexpected, not found\n");
                return false;
            }
        } catch (const std::bad_alloc &) {
            printf ("not enough memory.\n");
        }
    }
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
