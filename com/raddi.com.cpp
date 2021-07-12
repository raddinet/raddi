#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <stdexcept>
#include <csignal>
#include <cstdarg>
#include <cwctype>
#include <memory>

#include <sodium.h>
#include <lzma.h>
#include "../lib/cuckoocycle.h"

#include "../common/log.h"
#include "../common/lock.h"
#include "../common/file.h"
#include "../common/options.h"
#include "../common/platform.h"

#include "../core/raddi.h"
#include "../core/raddi_request.h"
#include "../core/raddi_content.h"
#include "../core/raddi_defaults.h"
#include "../core/raddi_peer_levels.h"
#include "../core/raddi_database_peerset.h"

#pragma warning (disable:6262) // function stack size warning

uuid app;
static union {
    wchar_t         buffer [4 * 65536];
    std::uint8_t rawbuffer [8 * 65536];
};
volatile bool quit = false;

alignas (std::uint64_t) char raddi::protocol::magic [8] = "RADDI/1";
const VS_FIXEDFILEINFO * const version = GetCurrentProcessVersionInfo ();

int argc;
wchar_t ** argw;

// TODO: add last, offline, attempt to find database for offline read access
// TODO: add support for entering data in Base64 (sodium_base642bin)
// TODO: use 'nstring' here to abstract Windows stuff out

template <typename T> struct hexadecimal_char_traits {};
template <> struct hexadecimal_char_traits <char> {
    static int is (char c) { return std::isxdigit (c); }
    static int scan (const char * input, unsigned int * value, int * offset) {
        return std::sscanf (input, "%2x%n", value, offset);
    }
};
template <> struct hexadecimal_char_traits <wchar_t> {
    static int is (wchar_t c) { return std::iswxdigit (c); }
    static int scan (const wchar_t * input, unsigned int * value, int * offset) {
        return std::swscanf (input, L"%2x%n", value, offset);
    }
};

// hexadecimal
//  - parses string of two-character hexadecimal values
//    optionally separated by any non-hexadecimal character
//  - returns number of bytes written to 'content'
//
template <typename C>
std::size_t hexadecimal (const C * p, std::uint8_t * content, std::size_t maximum) {
    std::size_t n = 0;

    while (*p && maximum) {
        while (*p && !hexadecimal_char_traits <C>::is (*p)) {
            ++p;
        }
        int offset;
        unsigned int value;
        if (hexadecimal_char_traits <C>::scan (p, &value, &offset) == 1) {
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

void color (int c) {
#ifdef _WIN32
    SetConsoleTextAttribute (GetStdHandle (STD_OUTPUT_HANDLE), c);
#endif
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
//  - TODO: warning when 'maximum' is exceeded
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
//  - TODO: rewrite to support streaming content (e.g. 'hash' command and large files)
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
    raddi::log::display ((argc > 1) ? option (argc, argw, L"display") : L"event");
    raddi::log::initialize (option (argc, argw, L"log"), raddi::defaults::log_subdir, L"cmd", raddi::log::scope::user);

    raddi::log::event (0x01,
                       (unsigned long) HIWORD (version->dwProductVersionMS),
                       (unsigned long) LOWORD (version->dwProductVersionMS),
                       ARCHITECTURE, BUILD_TIMESTAMP, COMPILER);

	if (auto h = LoadLibrary (SQLITE3_DLL_NAME)) {
        // raddi.com does not use sqlite, but logs version number for sake of completeness
        if (auto p = reinterpret_cast <const char * (*) ()> (GetProcAddress (h, "sqlite3_libversion"))) {
            raddi::log::note (0x05, "sqlite3", p (), SQLITE3_DLL_TYPE);
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
            raddi::log::event (0x02, app);
        }
        RegCloseKey (registry);
        registry = NULL;
    }

    WSADATA wsa;
    if (auto error = WSAStartup (0x0202, &wsa)) {
        raddi::log::error (0xFF, raddi::log::api_error (error));
        return error;
    }

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
bool hash (const wchar_t *);

bool new_identity ();
bool new_channel ();
bool reply (const wchar_t * opname, const wchar_t * to, bool thread = false);

bool installer (const wchar_t * svcname, bool install);
bool list_identities ();
bool list_channels ();
bool list_threads ();
bool list_peers (raddi::level);
bool list (const wchar_t * parent);
bool get (const wchar_t * eid);
bool analyze (const std::uint8_t * data, std::size_t size, bool compact, std::size_t nesting = 0);

bool set_log_level (const wchar_t * level);
bool set_display_level (const wchar_t * level);
bool download_command (const wchar_t * what);
bool erase_command (const wchar_t * what);
bool peer_command (enum class raddi::command::type, const wchar_t * address);
bool subscription_command (enum class raddi::command::type, const wchar_t * eid);

std::uint32_t parse_timestamp (const wchar_t * parameter) {
    if (parameter [0]) {
        wchar_t * end = nullptr;
        auto value = std::wcstoull (parameter, &end, 16);
        if ((value <= 0xFFFFFFFF) && (*end == L'\0')) {
            return (std::uint32_t) value;
        } else {
            raddi::log::error (0x1C, parameter);
            return 0;
        }
    } else
        return raddi::now ();
}

template <std::size_t N>
const wchar_t * command (unsigned long argc, wchar_t ** argw, const wchar_t (&name) [N], const wchar_t * default_ = nullptr) {
    if (argc > 1) {
        option_nth (1, argc, argw, name,
                    [&default_](const wchar_t * result) { default_ = result;  });
    }
    return default_;
}

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

    // TODO: clear blacklist, list blacklist, ban:IP?
    // TODO: clear peers (all)

    // install/uninstall
    //  - create or remove system service

    if (auto parameter = command (argc, argw, L"install")) return installer (parameter, true);
    if (auto parameter = command (argc, argw, L"uninstall")) return installer (parameter, false);

    // now/timestamp/microtimestamp
    //  - will show current time in raddi format (hexadecimal since 1.1.2018, see raddi_timestamp.h)
    //  - or converts timestamp to hierarchical datetime format: YYYY-MM-DD HH:MM:SS[.uuuuuu]

    if (command (argc, argw, L"now")) {
        std::printf ("%lx\n", raddi::now ());
        return true;
    }
    if (auto timestamp = command (argc, argw, L"microtimestamp")) {
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
    if (auto parameter = command (argc, argw, L"timestamp")) {
        if (auto timestamp = parse_timestamp (parameter)) {
            auto time = raddi::time ((std::uint32_t) timestamp);
            std::printf ("%04d-%02d-%02d %02d:%02d:%02d\n",
                         time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
                         time.tm_hour, time.tm_min, time.tm_sec);
            return true;
        } else
            return false;
    }

    // deepscan
    //  - this is very rough verification of the whole database

    if (command (argc, argw, L"deepscan")) {
        return database_verification ();
    }

    // benchmark
    //  - measures how long it takes to find predetermined solution

    if (command (argc, argw, L"benchmark")) {
        return benchmark ();
    }

    if (auto parameter = command (argc, argw, L"hash")) {
        return hash (parameter);
    }

    if (auto parameter = command (argc, argw, L"list")) {
        if (std::wcscmp (parameter, L"instances") == 0) {
            auto instances = raddi::instance::enumerate ();
            if (!instances.empty ()) {
                for (auto & [pid, description] : instances) {
                    std::printf ("%5u (%u): %s; priority: %u\n", pid, description.session,
                                 description.running
                                 ? description.broadcasting
                                 ? "broadcasting"
                                 : "disconnected"
                                 : "crashed",
                                 description.priority);
                }
            } else {
                std::printf ("no instances found\n");
            }
            return true;
        }
        if (std::wcscmp (parameter, L"identities") == 0) {
            return list_identities ();
        }
        if (std::wcscmp (parameter, L"channels") == 0) {
            return list_channels ();
        }
        if (std::wcscmp (parameter, L"threads") == 0) {
            return list_threads ();
        }
        if (std::wcscmp (parameter, L"peers") == 0) {
            return list_peers (raddi::level::core_nodes)
                && list_peers (raddi::level::established_nodes)
                && list_peers (raddi::level::validated_nodes)
                && list_peers (raddi::level::announced_nodes) // ???
                ;
        }
        if (std::wcscmp (parameter, L"core") == 0) {
            return list_peers (raddi::level::core_nodes);
        }
        if (std::wcscmp (parameter, L"blacklist") == 0) {
            return list_peers (raddi::level::blacklisted_nodes);
        }

        // TODO: rejected
        // TODO: subscriptions/blacklisted/retained

        return list (parameter);
    }

    if (auto parameter = command (argc, argw, L"set-log-level")) {
        return set_log_level (parameter);
    }
    if (auto parameter = command (argc, argw, L"set-display-level")) {
        return set_display_level (parameter);
    }

    if (auto parameter = command (argc, argw, L"new")) {
        if (!std::wcscmp (parameter, L"identity")) {
            return new_identity ();
        }
        if (!std::wcscmp (parameter, L"channel")) {
            return new_channel ();
        }
        if (!std::wcscmp (parameter, L"thread")) {
            if (auto to = option (argc, argw, L"channel")) {
                // TODO: verify that 'to' is a channel
                return reply (L"new:thread", to, true);
            }
        }
    }

    if (auto parameter = command (argc, argw, L"reply")) {
        return reply (L"reply", parameter, false);
    }

    if (auto parameter = command (argc, argw, L"get")) {
        return get (parameter);
    }
    if (auto parameter = command (argc, argw, L"analyze")) {
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
            return analyze (data, size, false);
        else
            return false;
    }

    // TODO: document all below

    if (auto parameter = command (argc, argw, L"echo")) {
        if (std::wcscmp (parameter, L"connection") == 0) {

            raddi::instance instance (option (argc, argw, L"instance"));
            if (instance.status != ERROR_SUCCESS)
                return raddi::log::data (0x91);
            else
                return send (instance, raddi::command::type::log_conn_status);
        }
    }

    if (auto parameter = command (argc, argw, L"add")) {
        bool core = false;
        option (argc, argw, L"core", core);
        return peer_command (core ? raddi::command::type::add_core_peer : raddi::command::type::add_peer, parameter);
    }
    if (auto parameter = command (argc, argw, L"remove")) {
        return peer_command (raddi::command::type::rem_peer, parameter);
    }
    if (auto parameter = command (argc, argw, L"ban")) {
        return peer_command (raddi::command::type::ban_peer, parameter);
    }
    if (auto parameter = command (argc, argw, L"unban")) {
        return peer_command (raddi::command::type::unban_peer, parameter);
    }
    if (auto parameter = command (argc, argw, L"connect")) {
        return peer_command (raddi::command::type::connect_peer, parameter);
    }

    if (auto parameter = command (argc, argw, L"download")) {
        return download_command (parameter);
    }
    if (auto parameter = command (argc, argw, L"erase")) {
        return erase_command (parameter);
    }

    if (auto parameter = command (argc, argw, L"subscribe")) {
        return subscription_command (raddi::command::type::subscribe, parameter);
    }
    if (auto parameter = command (argc, argw, L"unsubscribe")) {
        return subscription_command (raddi::command::type::unsubscribe, parameter);
    }
    if (auto parameter = command (argc, argw, L"blacklist")) {
        return subscription_command (raddi::command::type::blacklist, parameter);
    }
    if (auto parameter = command (argc, argw, L"unblacklist")) { // TODO: better name?
        return subscription_command (raddi::command::type::unblacklist, parameter);
    }
    if (auto parameter = command (argc, argw, L"retain")) {
        return subscription_command (raddi::command::type::retain, parameter);
    }
    if (auto parameter = command (argc, argw, L"unretain")) { // TODO: better name?
        return subscription_command (raddi::command::type::unretain, parameter);
    }
    
    std::printf ("\n");
    std::printf (">> https://www.raddi.net/\n");
    std::printf (">> https://github.com/raddinet/\n");
    std::printf ("\n");
    std::printf ("See docs/parameters.txt for supported commands and parameters\n\n");
    return false;
}

bool installer (const wchar_t * svcname, bool install) {
    bool result = false;
    if (auto scm = OpenSCManager (NULL, NULL, STANDARD_RIGHTS_WRITE | SC_MANAGER_CREATE_SERVICE)) {
        
        // unless overriden, use default service name

        if (svcname [0] == L'\0') {
            svcname = raddi::defaults::service_name; // "raddi"
        }

        // path
        //  - if not provided, rewrite path/raddi.com (this executable) => path/raddi64.exe or path/raddi32.exe (node software)

        const wchar_t * path = nullptr;
        if (auto parameter = option (argc, argw, L"path")) {
            if (parameter [0]) {
                path = parameter;
            } else
                return false;
        } else {
            if (GetModuleFileName (NULL, buffer, sizeof buffer / sizeof buffer [0])) {
                if (auto dot = std::wcsrchr (buffer, L'.')) {
                    *dot = L'\0';
                }
#ifdef _WIN64
                std::wcscat (buffer, L"64.exe");
#else
                std::wcscat (buffer, L"32.exe");
#endif
                path = buffer;
            } else
                return false;
        }

        if (install) {
            if (auto svc = CreateService (scm, svcname, raddi::defaults::service_title,
                                          SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                                          SERVICE_ERROR_NORMAL, path, NULL, NULL, L"LanmanServer", NULL, NULL)) {
                result = true;
                CloseServiceHandle (svc);
            }
        } else {
            if (auto svc = OpenService (scm, svcname, SERVICE_ALL_ACCESS)) {
                result = DeleteService (svc);
                CloseServiceHandle (svc);
            }
        }
        CloseServiceHandle (scm);
    }
    if (result) {
        std::printf ("done.\n");
    }
    return result;
}

bool StringToAddress (SOCKADDR_INET & address, const wchar_t * string) noexcept {
    std::memset (&address, 0, sizeof address);
    INT length = sizeof address;
    return WSAStringToAddress (const_cast <wchar_t *> (string), AF_INET6, NULL, reinterpret_cast <SOCKADDR *> (&address.Ipv6), &length) == 0
        || WSAStringToAddress (const_cast <wchar_t *> (string), AF_INET, NULL, reinterpret_cast <SOCKADDR *> (&address.Ipv4), &length) == 0;
}

bool set_log_level (const wchar_t * parameter) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::log::level level;
    if (raddi::log::parse_level (parameter, &level)) {
        return send (instance, raddi::command::type::set_log_level, level);
    } else
        return false;
}
bool set_display_level (const wchar_t * parameter) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::log::level level;
    if (raddi::log::parse_level (parameter, &level)) {
        return send (instance, raddi::command::type::set_display_level, level);
    } else
        return false;
}

bool peer_command (enum class raddi::command::type cmd, const wchar_t * addr) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    // TODO: DNS

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

    return send (instance, quick ? raddi::command::type::erase : raddi::command::type::erase_thorough, entry);
}

template <typename T>
std::size_t sign_and_validate (const wchar_t * step, T & entry, std::size_t size,
                               const std::uint8_t (&seed) [crypto_sign_ed25519_SEEDBYTES],
                               const std::uint8_t (&pk) [crypto_sign_ed25519_PUBLICKEYBYTES],
                               raddi::proof::requirements rq) {
    
    std::uint8_t sk [crypto_sign_ed25519_SECRETKEYBYTES];
    std::memcpy (sk, seed, crypto_sign_ed25519_SEEDBYTES);
    std::memcpy (sk + crypto_sign_ed25519_SEEDBYTES, pk, crypto_sign_ed25519_PUBLICKEYBYTES);

    try {
        if (auto a = entry.sign (sizeof entry + size, sk,
                                 complexity (rq), &quit)) {
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
    if (auto parameter = option (argc, argw, L"timestamp")) {
        raddi::log::data (0x97, parameter);
    }

    while (!quit) {
        std::uint8_t private_key [crypto_sign_ed25519_SEEDBYTES];
        
        auto timestamp = raddi::now ();
        if (auto parameter = option (argc, argw, L"timestamp")) {
            if (!(timestamp = parse_timestamp (parameter)))
                return false;
        }
        if (announcement.create (private_key, timestamp)) {

            if (auto size = sign_and_validate <raddi::identity> (L"new:identity", announcement, description_size,
                                                                 private_key, announcement.public_key, announcement.default_requirements ())) {
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

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    if (auto parameter = option (argc, argw, L"timestamp")) {
        raddi::log::data (0x97, parameter);
    }

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
                auto timestamp = raddi::now ();
                if (auto parameter = option (argc, argw, L"timestamp")) {
                    if (!(timestamp = parse_timestamp (parameter)))
                        return false;
                }
                if (announcement.create (id, timestamp)) {
                    if (auto size = sign_and_validate <raddi::channel> (L"new:channel", announcement, description_size,
                                                                        key, parent.public_key, announcement.default_requirements ())) {
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

bool reply (const wchar_t * opname, const wchar_t * to, bool thread) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    if (auto parameter = option (argc, argw, L"timestamp")) {
        raddi::log::data (0x97, parameter);
    }

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
            raddi::proof::requirements rq;

            if (thread) {
                if (raddi::content::is_plain_line (message.description, description_size)) {
                    if (description_size > raddi::consensus::max_thread_name_size) {
                        return raddi::log::error (0x1D, description_size, raddi::consensus::max_thread_name_size);
                    }
                } else {
                    // 'threads' which are channel metadata are strictly limited in size
                    if (description_size > raddi::consensus::max_channel_control_size) {
                        return raddi::log::error (0x1D, description_size, raddi::consensus::max_channel_control_size);
                    }
                }

                rq.time = raddi::consensus::min_thread_pow_time;
                rq.complexity = raddi::consensus::min_thread_pow_complexity;
            } else {
                rq.time = raddi::consensus::min_entry_pow_time;
                rq.complexity = raddi::consensus::min_entry_pow_complexity;
            }

            while (!quit) {
                message.id.timestamp = raddi::now ();
                message.id.identity = id;
                 
                if (auto parameter = option (argc, argw, L"timestamp")) {
                    if (!(message.id.timestamp = parse_timestamp (parameter)))
                        return false;
                }

                if (auto size = sign_and_validate <raddi::entry> (opname, message, description_size,
                                                                  key, identity.public_key, rq)) {
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

bool list (const wchar_t * parent_) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    bool names = true;
    option (argc, argw, L"names", names);

    std::uint32_t oldest = 0;
    std::uint32_t latest = raddi::now ();

    if (auto p = option (argc, argw, L"oldest")) {
        oldest = std::wcstoul (p, nullptr, 16);
    }
    if (auto p = option (argc, argw, L"latest")) {
        latest = std::wcstoul (p, nullptr, 16);
    }

    raddi::eid parent;
    raddi::iid author;

    if (!parent.parse (parent_))
        return raddi::log::data (0x92, parent_);

    if (auto iid = option (argc, argw, L"author")) {
        if (!author.parse (iid))
            return raddi::log::data (0x95, iid);

        if (!database.identities->get (author))
            return raddi::log::data (0x96, author);
    } else {
        author.timestamp = 0;
        author.nonce = 0;
    }

    list_column_info columns [] = {
        { 24, "%*ls", "eid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*llu", "n" },
        { 9, "%*u", "offset" },
        { 5, "%*u", "size" },
        { 22, "%-*ls", "content" },
    };
    for (auto column : columns) {
        if (std::strchr (column.format, '-')) {
            std::printf (" %-*s", column.width, column.name);
        } else {
            std::printf ("%*s", column.width, column.name);
        }
    }
    std::printf ("\n");

    database.data->select (oldest, latest,
                           [parent, author] (const auto & row, const auto &) {
                                return row.parent == parent
                                    && (author.isnull () || row.id.identity == author);
                           },
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
                           [&columns] (const auto & row, const auto &, std::uint8_t * raw) {
                                const auto entry = reinterpret_cast <raddi::entry *> (raw);

                                std::size_t size = row.data.length;
                                std::size_t proof_size = 0;
                                if (entry->proof (size + sizeof (raddi::entry), &proof_size)) {
                                    std::printf (" ");
                                    analyze (entry->content (), size - proof_size, true);
                                    std::printf ("\n");
                                } else
                                    raddi::log::stop (0x21, row.id);
                            });
    return true;
}

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
                        std::size_t size = row.data.length;

                        if (entry->proof (size + sizeof (raddi::entry), &proof_size)) {

                            auto analysis = raddi::content::analyze (entry->content () + skip, size - proof_size - skip);
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

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    list_column_info columns [] = {
        { 16, "%*ls", "iid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*zu", "n" },
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

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    list_column_info columns [] = {
        { 24, "%*ls", "eid" },
        { 9, "%*x", "shard" },
        { 5, "%*u", "i" },
        { 5, "%*zu", "n" },
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

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
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
        { 5, "%*zu", "n" },
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
bool list_peers (raddi::level level) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
    if (!database.connected ())
        return raddi::log::error (0x92, instance.get <std::wstring> (L"database"));

    try {
        std::wprintf (L"%s:\n", raddi::translate (level, std::wstring ()).c_str ());

        database.peers [level]->load (database.path + L"\\network", AF_INET, level);
        database.peers [level]->load (database.path + L"\\network", AF_INET6, level);

        if (!database.peers [level]->enumerate ([](const raddi::address & a, std::uint16_t assessment) {
            auto base = 5;
            switch (a.family) {
                case AF_INET: base = 21; break;
                case AF_INET6: base = 47; break;
            }
            auto padding = 0;
            padding += (a.port < 10000u);
            padding += (a.port < 1000u);
            padding += (a.port < 100u);
            padding += (a.port < 10u);
            padding += 2 * (a.port == 0u);

            std::wprintf (L"%*s%*s (%u)\n", base - padding, raddi::translate (a, std::wstring ()).c_str (), padding, L"", assessment);
        })) {
            std::printf ("-\n");
        }
        return true;
    } catch (const std::bad_alloc &) {
        raddi::log::error (raddi::component::database, 20);
    }
    return false;
}

bool analyze (const std::uint8_t * data, std::size_t size, bool compact, std::size_t nesting) {
    option (argc, argw, L"compact", compact);

    char indent [9];
    char indent_extra [10];
    if (compact) {
        indent [0] = '\0';
        indent_extra [0] = '\0';
    } else {
        for (auto i = 0u; i != std::min (nesting, sizeof indent - 1); ++i) {
            indent [i] = '\t';
        }
        for (auto i = 0u; i != std::min (nesting + 1, sizeof indent_extra - 1); ++i) {
            indent_extra [i] = '\t';
        }
        indent [std::min (nesting, sizeof indent - 1)] = '\0';
        indent_extra [std::min (nesting + 1, sizeof indent_extra - 1)] = '\0';
    }

    auto analysis = raddi::content::analyze (data, size);

    if (!analysis.marks.empty ()) {
        if (!compact) {
            color (7);
            std::printf ("%sMARKS:\n\t%s", indent, indent);
        }
        for (const auto & marker : analysis.marks) {
            switch (marker.type) {
                case 0x1C: // FS, table  --.
                case 0x1D: // GS, tab (?)--+-- used to render tables
                case 0x1E: // RS, row    --|
                case 0x1F: // US, column --'
                    continue; // they affect GUI rendering, don't show here
            }
            if (marker.defined) {
                switch (marker.type) {
                    case 0x06:
                        // green
                        color (2); std::printf ("ACK ");
                        break;
                    case 0x07:
                         // red
                        color (4); std::printf ("REPORT ");
                        break;
                    case 0x08:
                        // red
                        color (4); std::printf ("REVERT ");
                        break;
                    case 0x7F:
                        // red
                        color (4); std::printf ("DELETE ");
                        break;
                }
            } else {
                // pink
                color (5); std::printf ("0x%02x ", marker.type);
            }

            if (marker.insertion && !compact) {
                std::printf ("@%u ", marker.insertion);
                // TODO: remember and display symbol in text, different color
            }
        }
        if (!compact) {
            std::printf ("\n");
        }
    }

    if (!analysis.tokens.empty ()) {
        if (!compact) {
            color (7);
            std::printf ("%sTOKENS:\n", indent);
        }
        for (const auto & token : analysis.tokens) {
            bool code = false;
            bool type = true;

            if (token.defined) {
                bool named = true;
                switch (token.type) {
                    case 0x0B:
                        if (token.code || !token.string) {
                            switch (token.code) {
                                case 0x01: color (9); std::printf ("%sUPVOTE", indent_extra); break;
                                case 0x02: color (12); std::printf ("%sDOWNVOTE", indent_extra); break;
                                case 0x03: color (14 /*?*/); std::printf ("%sINFORMATIVE (vote)", indent_extra); break;
                                case 0x04: color (13 /*?*/); std::printf ("%sFUNNY (vote)", indent_extra); break;
                                case 0x05: color (4); std::printf ("%sSPAM (vote)", indent_extra); break;
                                default:
                                    code = true;
                                    named = false;
                            }
                        }
                        if (!named) {
                            color (5);
                            std::printf ("%sVOTE:", indent_extra);
                        }
                        break;
                    case 0x10:
                        if (token.code || !token.string) {
                            switch (token.code) {
                                case 0x01: color (5); std::printf ("%sTITLE", indent_extra); break;
                                case 0x04: color (5); std::printf ("%sSIDEBAR", indent_extra); break;
                                default:
                                    code = true;
                                    named = false;
                            }
                        }
                        if (!named) {
                            color (5);
                            std::printf ("%sSIDEBAND:", indent_extra);
                        }
                        break;
                    case 0x15:
                        if (token.code || !token.string) {
                            switch (token.code) {
                                case 0x01: color (4); std::printf ("%sHIDE", indent_extra); break;
                                case 0x02: color (4); std::printf ("%sBAN", indent_extra); break;
                                case 0x03: color (12); std::printf ("%sNSFW", indent_extra); break;
                                case 0x04: color (12); std::printf ("%sNSFL", indent_extra); break;
                                case 0x05: color (4); std::printf ("%sSPOILER", indent_extra); break;
                                case 0x08: color (9); std::printf ("%sSTICK", indent_extra); break;
                                case 0x09: color (9); std::printf ("%sHIGHLIGHT", indent_extra); break;

                                default:
                                    code = true;
                                    named = false;
                            }
                        }
                        if (!named) {
                            color (5);
                            std::printf ("%sMODERATION:", indent_extra);
                        }
                        break;
                    default:
                        code = true;
                        type = false;
                }
            } else {
                type = false;
            }

            if (!type) {
                // pink
                color (5); std::printf ("%s0x%02x:", indent_extra, token.type);
                code = true;
            }

            if (code && (token.code || !token.string)) {
                std::printf ("0x%02x", token.code);
            }
            if (token.string) {
                std::wprintf (L"%s", u82ws (token.string, token.string_end - token.string).c_str ());
            }
            if (!compact) {
                if (token.insertion) {
                    std::printf (" @%u", token.insertion);
                    // TODO: remember and display symbol in text, different color
                }
                if (token.truncated) {
                    std::printf ("-TRUNCATED!");
                }
            }
            if (compact) {
                std::printf (" ");
            } else {
                std::printf ("\n");
            }
        }
    }

    if (!analysis.stamps.empty ()) {
        if (!compact) {
            color (7);
            std::printf ("%sSTAMPS:\n", indent);
        }
        for (const auto & stamp : analysis.stamps) {
            bool code = false;
            bool type = true;

            color (7);
            if (stamp.defined) {
                switch (stamp.type) {
                    case 0x16: // SYN, chaining
                        switch (stamp.code) {
                            case 0x10: std::printf ("%sPARENT CRC-16 CCITT", indent_extra); break;
                            case 0x20: std::printf ("%sPARENT CRC-32", indent_extra); break;
                            case 0x30: std::printf ("%sPARENT CRC-64", indent_extra); break;
                            case 0x51: std::printf ("%sPARENT BLAKE2b (16B)", indent_extra); break;
                            case 0x61: std::printf ("%sPARENT BLAKE2b (32B)", indent_extra); break;
                            case 0x71: std::printf ("%sPARENT BLAKE2b (64B)", indent_extra); break;
                            case 0x32: std::printf ("%sPARENT SipHash24", indent_extra); break;
                            case 0x52: std::printf ("%sPARENT SipHashx24", indent_extra); break;
                            case 0x63: std::printf ("%sPARENT SHA-256", indent_extra); break;
                            case 0x73: std::printf ("%sPARENT SHA-512", indent_extra); break;
                            default:
                                code = true;
                                type = false;
                        }
                        break;
                    case 0x19: // EM, endorsement
                        switch (stamp.code) {
                            case 0x00: std::printf ("%sENDORSE PARENT", indent_extra); break;
                            case 0x30: std::printf ("%sENDORSE IID", indent_extra); break;
                            case 0x40: std::printf ("%sENDORSE EID", indent_extra); break;
                            default:
                                code = true;
                                type = false;
                        }
                        break;
                    default:
                        type = false;
                }
            } else {
                type = false;
            }

            if (!type) {
                // pink
                color (5); std::printf ("%s0x%02x:", indent_extra, stamp.type);
                code = true;
            }
            if (code) {
                std::printf ("0x%02x", stamp.code);
            }
            if (stamp.data && stamp.size) {
                if (stamp.truncated) {
                    color (5);
                    std::printf (" [%u] ", stamp.size);
                } else {
                    std::printf (": ");
                }
                if (!stamp.truncated && (stamp.type == 0x19) && ((stamp.code == 0x30) || (stamp.code == 0x40))) {
                    if (stamp.code == 0x30) {
                        std::printf ("%ls", reinterpret_cast <const raddi::iid *> (stamp.data)->serialize ().c_str ());
                    } else {
                        std::printf ("%ls", reinterpret_cast <const raddi::eid *> (stamp.data)->serialize ().c_str ());
                    }
                } else {
                    for (std::size_t i = 0; i != stamp.size; ++i) {
                        std::printf ("%02x", stamp.data [i]);
                    }
                }
            }
            if (!compact) {
                if (stamp.insertion) {
                    std::printf (" @%u", stamp.insertion);
                    // TODO: remember and display symbol in text, different color
                }
                if (stamp.truncated) {
                    std::printf ("-TRUNCATED!");
                }
            }
            if (compact) {
                std::printf (" ");
            } else {
                std::printf ("\n");
            }
        }
    }
    if (!analysis.attachments.empty ()) {
        if (!compact) {
            color (7);
            std::printf ("%sATTACHMENTS:\n", indent);
        }
        for (const auto & attachment : analysis.attachments) {
            bool type = true;
            if (attachment.defined) {
                switch (attachment.type) {
                    case 0xFA: color (1); std::printf ("%sBINARY: ", indent_extra); break;
                    case 0xFC: color (1); std::printf ("%sCOMPRESSED: ", indent_extra); break;
                    case 0xFE: color (1); std::printf ("%sENCRYPTED: ", indent_extra); break;
                    case 0xFF: color (1); std::printf ("%sPRIVATE MESSAGE: ", indent_extra); break;
                    default:
                        type = false;
                }
            } else {
                type = false;
            }
            if (!type) {
                color (5); std::printf ("%s0x%02x: ", indent_extra, attachment.type);
            }

            std::printf ("%u bytes", attachment.size);

            if (!compact) {
                if (attachment.insertion) {
                    std::printf (" @%u", attachment.insertion);
                    // TODO: remember and display symbol in text, different color
                }
                if (attachment.truncated) {
                    std::printf (" TRUNCATED!");
                }
            }
            if (attachment.type == 0xFC) {
                // if (compact) { std::printf (" ["); }
                // color (7);
                // TODO: decompress and analyze (..., nesting + 1)
                // if (compact) { std::printf ("] "); }
            }
            if (compact) {
                std::printf (" ");
            } else {
                std::printf ("\n");
            }
        }
    }

    for (const auto & edit : analysis.edits) {
        if (compact) {
            color (14);
            std::printf ("EDIT %04x..%04x [",
                         edit.offset, edit.offset + edit.length);
            color (7);
            analyze (edit.string, edit.string_end - edit.string, compact, nesting + 1);

            color (14);
            std::printf ("] ");
        } else {
            color (7);
            std::printf ("%sEDIT: offset %u, %u bytes, replace with following %zu bytes%s:\n",
                         indent, edit.offset, edit.length, edit.string_end - edit.string, edit.truncated ? " (truncated)" : "");

            analyze (edit.string, edit.string_end - edit.string, compact, nesting + 1);

            std::printf ("\n");
        }
    }
    
    if (!analysis.text.empty ()) {
        color (7);
        if (!compact) {
            std::printf ("%sTEXT:\n%s", indent, indent);
        }
        for (const auto & text : analysis.text) {
            if (!compact) {
                if (text.paragraph) {
                    std::printf ("\n\n%s", indent);
                }
            }
            if (text.heading) {
                color (15 | BACKGROUND_INTENSITY);
            } else {
                color (7);
            }
            std::wstring ws = u82ws (text.begin, (std::size_t) (text.end - text.begin));
            std::wstring::size_type i = 0;
            while ((i = ws.find (L'\x0D', i)) != std::wstring::npos) {
                ws.insert (i + 1, 1, L'\x0A');
                i += 2;
            }
            std::wprintf (L"%s", ws.c_str ());
        }
        std::printf ("\n");
    }

    color (7);

    if (analysis.padding) {
        std::printf ("%sPADDING: %zu\n", indent, analysis.padding);
    }

    return true;
}

bool get (const wchar_t * what) {
    raddi::instance instance (option (argc, argw, L"instance"));
    if (instance.status != ERROR_SUCCESS)
        return raddi::log::data (0x91);

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
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

            return analyze (entry->content () + skip, size - proof_size - skip, false);
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

    raddi::db database (file::access::read, instance.get <std::wstring> (L"database"));
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

std::string simplify_hash_name (const wchar_t * s) {
    std::string result;
    for (; *s; ++s) {
        switch (*s) {
            case L'-':
            case L'_':
            case L':':
                break;
            default:
                if (std::iswalpha (*s)) {
                    result.push_back (char (std::towlower (*s)));
                } else
                if (std::iswdigit (*s)) {
                    result.push_back (char (*s));
                } else
                    return result;
        }
    }
    return result;
}

namespace {
    std::uint16_t crc16 (const std::uint8_t * data, std::size_t size, std::uint16_t r) {
        while (size--) {
            r ^= *data++;

            for (auto bit = 0u; bit != 8; ++bit) {
                if (r & 0x0001) {
                    r >>= 1;
                    r ^= 0x8408u; // CCITT
                } else {
                    r >>= 1;
                }
            }
        }
        return r;
    }
}

bool hash (const wchar_t * function_) {
    const auto function = simplify_hash_name (function_);
    const auto datasize = gather (rawbuffer, sizeof rawbuffer);

    auto n = 0u;
    std::uint8_t result [64];

    if (function == "crc16") {
        auto crc = crc16 (rawbuffer, datasize, 0);
        n = sizeof crc;
        std::memcpy (result, &crc, n);
    }
    if (function == "crc32") {
        auto crc = lzma_crc32 (rawbuffer, datasize, 0); // IEEE 802.3
        n = sizeof crc;
        std::memcpy (result, &crc, n);
    }
    if (function == "crc64") {
        auto crc = lzma_crc64 (rawbuffer, datasize, 0); // ECMA-182
        n = sizeof crc;
        std::memcpy (result, &crc, n);
    }

    if (function.starts_with ("blake2b")) {
        try {
            n = std::stoul (function.substr (7));
            if (n >= 16 && n <= 64) {
                crypto_generichash_blake2b (result, n, rawbuffer, datasize, nullptr, 0);
            } else
                return false;
        } catch (...) {
            return false;
        }
    }

    if (function == "siphash03raddi") {
        std::uint8_t seed [32] = { 0 };
        cuckoo::hash <0, 3> hash;

        hash.seed (seed);
        auto h = hash (rawbuffer, datasize);
        
        n = sizeof h;
        std::memcpy (result, &h, n);
    }

    if (function == "siphash24") {
        n = crypto_shorthash_siphash24_BYTES;
        std::uint8_t blank [crypto_shorthash_siphash24_KEYBYTES] = { 0 };
        crypto_shorthash_siphash24 (result, rawbuffer, datasize, blank);
    }
    if (function == "siphashx24") {
        n = crypto_shorthash_siphashx24_BYTES;
        std::uint8_t blank [crypto_shorthash_siphashx24_KEYBYTES] = { 0 };
        crypto_shorthash_siphashx24 (result, rawbuffer, datasize, blank);
    }

    if (function == "sha256") {
        n = crypto_hash_sha256_BYTES;
        crypto_hash_sha256 (result, rawbuffer, datasize);
    }
    if (function == "sha512") {
        n = crypto_hash_sha512_BYTES;
        crypto_hash_sha512 (result, rawbuffer, datasize);
    }

    if (n) {
        for (auto i = 0u; i != n; ++i) {
            std::printf ("%02x", result [i]);
        }
        std::printf ("\n");
        return true;
    } else
        return false;
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
                printf ("found... %.2fs\n", (raddi::microtimestamp () - t0) / 1000000.0);
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
