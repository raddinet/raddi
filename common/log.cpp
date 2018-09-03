#include "log.h"
#include "lock.h"
#include "file.h"
#include "directory.h"

#include <ws2tcpip.h>
#include <mstcpip.h>
#include <winioctl.h>
#include <objbase.h>
#include <shlobj.h>

#include <algorithm>
#include <vector>
#include <cstdio>
#include <cwctype>

wchar_t raddi::log::path [768];
extern "C" IMAGE_DOS_HEADER __ImageBase;

#ifdef NDEBUG
raddi::log::level raddi::log::settings::level = raddi::log::level::error;
raddi::log::level raddi::log::settings::display = raddi::log::level::event;
#else
raddi::log::level raddi::log::settings::level = raddi::log::level::event;
raddi::log::level raddi::log::settings::display = raddi::log::level::all;
#endif

namespace {
    bool compare_prefix (const wchar_t * string, const wchar_t * prefix) {
        auto length = std::wcslen (prefix);
        return !std::wcsncmp (string, prefix, length)
            && !std::iswalnum (string [length]);
    }

    raddi::log::level parse_level (const wchar_t * string, raddi::log::level default_) {
        if (string) {
            static const struct {
                const wchar_t * prefix;
                raddi::log::level level;
            } map [] = {
                { L"all", raddi::log::level::all },
                { L"everything", raddi::log::level::all },
                { L"event", raddi::log::level::event },
                { L"events", raddi::log::level::event },
                { L"data", raddi::log::level::data },
                { L"error", raddi::log::level::error },
                { L"errors", raddi::log::level::error },
                { L"stop", raddi::log::level::stop },
                { L"stops", raddi::log::level::stop },
                { L"disable", raddi::log::level::disabled },
                { L"disabled", raddi::log::level::disabled },
            };
            for (const auto & mapping : map)
                if (compare_prefix (string, mapping.prefix))
                    return mapping.level;
        }
        return default_;
    }
    const wchar_t * component_name (raddi::component c) {
        switch (c) {
            case raddi::component::main: return L"main";
            case raddi::component::server: return L"server";
            case raddi::component::source: return L"source";
            case raddi::component::database: return L"db";
        }
        return L"?";
    }
    const wchar_t * loglevel_name (raddi::log::level l) {
        switch (l) {
            case raddi::log::level::note: return L"note";
            case raddi::log::level::event: return L"event";
            case raddi::log::level::data: return L"data";
            case raddi::log::level::error: return L"error";
            case raddi::log::level::stop: return L"stop";
        }
        return L"?";
    }

    lock linelock;
    file logfile;
    HANDLE output = GetStdHandle (STD_OUTPUT_HANDLE);

    void create (const wchar_t * path) {
        SetLastError (0);
        if (logfile.open (path, file::mode::always, file::access::write, file::share::full)) {
            if (logfile.created ()) {
                logfile.compress ();
                if (!logfile.write ("\xEF\xBB\xBF", 3)) {
                    logfile.close ();
                }
            } else {
                logfile.tail ();
            }
        }
    }

    void create (wchar_t * path, const wchar_t * subdir, const wchar_t * filename) {
        if (subdir) {
            std::wcsncat (path, subdir, 255);
            directory::create (path);
        }
        std::wcsncat (path, filename, 255);
        create (path);
    }

    constexpr static inline HMODULE GetCurrentModuleHandle () {
        return reinterpret_cast <HMODULE> (&__ImageBase);
    };
}


bool raddi::log::initialize (const wchar_t * option, const wchar_t * subdir, const wchar_t * prefix, bool service) {
    settings::level = parse_level (option, settings::level);

    api_error user_path_error;
    std::wstring user_path;

    api_error shell_folder_error;
    std::wstring shell_folder;

    wchar_t filename [256];

    SYSTEMTIME t;
    GetLocalTime (&t);
    _snwprintf (filename, sizeof filename / sizeof filename [0], L"%s%02u%02u.log", prefix, t.wYear - 2000, t.wMonth);
    
    SetLastError (0);
    if ((option != nullptr) && (option = std::wcschr (option, L':'))) {
        path [0] = L'\0';
        if (GetFullPathName (option + 1, sizeof path / sizeof path [0], path, NULL)) {
            create (path);
        }
        if (logfile.closed ()) {
            user_path_error = api_error ();
            user_path = path;
        }
    }

    // same as (local) database
    if (logfile.closed ()) {
        path [0] = L'\0';
        if (SHGetFolderPath (NULL, service ? CSIDL_COMMON_APPDATA : CSIDL_LOCAL_APPDATA, NULL, 0, path) == S_OK) {
            create (path, subdir, filename);
        }
    }

    // installation directory as a fallback
    if (logfile.closed ()) {
        shell_folder_error = api_error ();
        shell_folder = path;

        if (GetModuleFileName (NULL, path, sizeof path / sizeof path [0])) {
            if (auto * end = std::wcsrchr (path, L'\\')) {
                *(end + 1) = L'\0';
            }
            create (path, subdir, filename);
        }
    }
    
    // report error (screen only, obviously)
    if (logfile.closed ()) {
        raddi::log::report (raddi::component::main, raddi::log::level::error, 0xF1, path);
    }
    if (!user_path.empty ()) {
        raddi::log::report (raddi::component::main, raddi::log::level::error, 0xF2, user_path, user_path_error);
    }
    if (!shell_folder.empty ()) {
        raddi::log::report (raddi::component::main, raddi::log::level::error, 0xF3, shell_folder, shell_folder_error);
    }

    return !logfile.closed ();
}

void raddi::log::display (const wchar_t * option) {
    settings::display = parse_level (option, settings::display);

    if (settings::display != log::level::disabled) {
        output = GetStdHandle (STD_OUTPUT_HANDLE);
    } else {
        output = INVALID_HANDLE_VALUE;
    }
}

bool raddi::log::internal::evaluate_level (raddi::log::level l) {
    return l >= settings::display
        || l >= settings::level;
}

std::wstring raddi::log::internal::load_string (raddi::component c, raddi::log::level l, unsigned int id) {
    if (id < 256) {
        id = unsigned (c) | unsigned (l) | id;
    }

    const wchar_t * ptr = nullptr;
    if (auto size = LoadString (GetCurrentModuleHandle (), id, (wchar_t *) &ptr, 0))
        return std::wstring (ptr, ptr + size);
    else
        return std::wstring ();
}

void raddi::log::internal::deliver (component c, level l, const provider_name * provider,
                                    unsigned int id, std::wstring message) {
    SYSTEMTIME t;

    // TODO: this can become bottleneck, just push to queue here, and do write/display on separate thread

    if (l >= settings::level && !logfile.closed ()) {
        GetLocalTime (&t);

        wchar_t string [512];
        _snwprintf (string, sizeof string / sizeof string [0],
                    L"%04u-%02u-%02u %02u:%02u:%02u.%03u "
                    L"\t%s\t%X\t%s"
                    L"\t%-12S\t%012zx\t%-32s"
                    L"\t%u\t%s"
                    L"\r\n",
                    t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                    loglevel_name (l), id, component_name (c),
                    provider && provider->object ? provider->object : "", (std::size_t) provider, provider ? provider->instance.c_str () : L"",
                    GetCurrentThreadId (), message.c_str ());
        
        char buffer [1536];
        if (auto n = WideCharToMultiByte (CP_UTF8, 0, string, (int) std::wcslen (string), buffer, sizeof buffer, NULL, NULL)) {
            logfile.write (buffer, n);
        }
    }

    if (l >= settings::display && output != INVALID_HANDLE_VALUE) {
        DWORD n;
        WORD color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        WORD accent = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        WORD darker = FOREGROUND_INTENSITY;

        switch (l) {
            case level::all:
            case level::note:
                color = FOREGROUND_INTENSITY;
                accent = FOREGROUND_GREEN;
                darker = FOREGROUND_INTENSITY;
                break;
            case level::event:
                color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                accent = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                darker = FOREGROUND_INTENSITY;
                break;
            case level::data:
                color = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN;
                accent = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                darker = FOREGROUND_INTENSITY;
                break;
            case level::error:
            case level::stop:
            case level::disabled:
                color = FOREGROUND_INTENSITY | FOREGROUND_RED;
                accent = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
                darker = FOREGROUND_INTENSITY | FOREGROUND_RED;
                break;
        }

        GetLocalTime (&t);

        wchar_t time [16];
        _snwprintf (time, sizeof time / sizeof time [0], L"%02u:%02u:%02u ", t.wHour, t.wMinute, t.wSecond);

        exclusive guard (linelock);

        SetConsoleTextAttribute (output, color); WriteConsole (output, time, 9, &n, NULL);

        if (c != component::main || provider) {
            SetConsoleTextAttribute (output, darker); WriteConsole (output, L"[", 1, &n, NULL);
            SetConsoleTextAttribute (output, color);

            if (auto name = component_name (c)) {
                WriteConsole (output, name, (int) std::wcslen (name), &n, NULL);
            }
            if (provider) {
                if (provider->object) {
                    SetConsoleTextAttribute (output, darker); WriteConsole (output, L":", 1, &n, NULL);
                    SetConsoleTextAttribute (output, color); WriteConsoleA (output, provider->object, (int) std::strlen (provider->object), &n, NULL);
                }
                /*if (provider->object || !provider->instance.empty ()) {
                    auto pointer = translate (provider, std::wstring ());

                    SetConsoleTextAttribute (output, darker); WriteConsole (output, L":", 1, &n, NULL);
                    SetConsoleTextAttribute (output, color); WriteConsole (output, pointer.c_str (), (int) pointer.length (), &n, NULL);
                }*/
                if (!provider->instance.empty ()) {
                    SetConsoleTextAttribute (output, darker); WriteConsole (output, L":", 1, &n, NULL);
                    SetConsoleTextAttribute (output, color); WriteConsole (output, provider->instance.c_str (), (int) provider->instance.length (), &n, NULL);
                }
            }
            if (id < 256 && false) { // TODO: when do we display message ID?
                wchar_t ids [3];
                _snwprintf (ids, 3, L"%02X", id);
                SetConsoleTextAttribute (output, darker); WriteConsole (output, L"]", 1, &n, NULL);
                SetConsoleTextAttribute (output, color);  WriteConsole (output, ids, 2, &n, NULL);
                SetConsoleTextAttribute (output, darker); WriteConsole (output, L":  ", 3, &n, NULL);
            } else {
                SetConsoleTextAttribute (output, darker); WriteConsole (output, L"]  ", 3, &n, NULL);
            }
        }
        
        // break by L"\x201C" and L"\x201D" to colorize with accent

        std::wstring::size_type o1 = 0u;
        std::wstring::size_type o2 = 0u;
        while ((o2 = message.find (L'\x201C', o1)) != std::wstring::npos) {

            SetConsoleTextAttribute (output, color);
            WriteConsole (output, message.c_str () + o1, (int) (o2 - o1), &n, NULL);

            o1 = o2 + 1;
            o2 = message.find (L'\x201D', o1);

            if (o2 != std::wstring::npos) {
                SetConsoleTextAttribute (output, accent);
                WriteConsole (output, message.c_str () + o1, (int) (o2 - o1), &n, NULL);

                o1 = o2 + 1;
            }
        }

        SetConsoleTextAttribute (output, color);
        WriteConsole (output, message.c_str () + o1, (int) (message.length () - o1), &n, NULL);
        WriteConsole (output, L"\n", 1, &n, NULL);
        SetConsoleTextAttribute (output, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
}

std::wstring raddi::log::translate (raddi::log::api_error argument, const std::wstring &) {
    const struct {
        DWORD   flags;
        HMODULE handle;
    } sources [] = {
        { FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK | FORMAT_MESSAGE_FROM_SYSTEM,  NULL },
        { FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK | FORMAT_MESSAGE_FROM_HMODULE, GetModuleHandle (L"wininet") },
        { FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK | FORMAT_MESSAGE_FROM_HMODULE, GetModuleHandle (L"ntdll") },
    };

    auto offset = 0;
    wchar_t buffer [2048];

    if (argument.code > 0x4000u) {
        offset = _snwprintf (buffer, sizeof buffer / sizeof buffer [0], L"0x%08X: ", argument.code);
    } else {
        offset = _snwprintf (buffer, sizeof buffer / sizeof buffer [0], L"%u: ", argument.code);
    }

    for (auto i = 0u; i != sizeof sources / sizeof sources [0]; ++i) {
        if (auto n = FormatMessage (sources [i].flags, sources [i].handle, argument.code, 0,
                                    &buffer [offset], sizeof buffer / sizeof buffer [0] - offset, NULL)) {
            n += offset;
            while (n && std::iswspace (buffer [n - 1])) {
                --n;
            }
            buffer [n] = L'\0';
            return buffer;
        }
    }

    buffer [offset - 2] = L'\0';
    return buffer;
}

std::wstring raddi::log::translate (const void * argument, const std::wstring &) {
    
    // on x86-64 display only 48-bit pointer (12 nibbles)
    // ARM64?

    static constexpr auto n = (sizeof (std::size_t) == 8u) ? (12u) : (2u * sizeof (std::size_t));

    wchar_t pointer [n + 1];
    _snwprintf (pointer, sizeof pointer / sizeof pointer [0], L"%0*zx", (int) n, (std::size_t) argument);
    return pointer;
}

std::wstring raddi::log::translate (const char * argument, const std::wstring &) {
    std::wstring result;
    if (argument [0]) {
        auto size = std::strlen (argument);
        if (auto n = MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, argument, (int) size, NULL, 0)) {
            result.resize (n);
            MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, argument, (int) size, &result [0], n);
        };
    }
    return result;
}

std::wstring raddi::log::translate (const std::string & argument, const std::wstring &) {
    std::wstring result;
    if (argument [0]) {
        if (auto n = MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, argument.data (), (int) argument.length (), NULL, 0)) {
            result.resize (n);
            MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, argument.data (), (int) argument.length (), &result [0], n);
        };
    }
    return result;
}

std::wstring raddi::log::translate (const in_addr * address, const std::wstring &) {
    wchar_t sz [16];
    RtlIpv4AddressToString (address, sz);
    return sz;
}
std::wstring raddi::log::translate (const in6_addr * address, const std::wstring &) {
    wchar_t sz [48];
    RtlIpv6AddressToString (address, sz);
    return sz;
}

namespace {
    // TODO: std::wstring translate (ADDRESS_FAMILY family, const void * address, std::uint16_t port, std::uint32_t scope)


}

std::wstring raddi::log::translate (const sockaddr * address, const std::wstring & format) {
    switch (address->sa_family) {
        default:
            return L"?";

        case AF_INET: {
            auto s = translate (reinterpret_cast <const sockaddr_in *> (address)->sin_addr, format);
            if (auto port = reinterpret_cast <const sockaddr_in *> (address)->sin_port) {
                s.reserve (s.length () + 6);
                s.append (L":");
                s.append (std::to_wstring (ntohs (port)));
            }
            return s;
        }
        
        case AF_INET6: {
            auto s = translate (reinterpret_cast <const sockaddr_in6 *> (address)->sin6_addr, format);

            auto port = reinterpret_cast <const sockaddr_in6 *> (address)->sin6_port;
            auto scope = reinterpret_cast <const sockaddr_in6 *> (address)->sin6_scope_id;

            if (port || scope) {
                s.reserve (s.length () + (port ? 8 : 0) + (scope ? 3 : 0));

                if (port) {
                    s.insert (s.begin (), L'[');
                    s.append (L"]:");
                    s.append (std::to_wstring (ntohs (port)));
                }
                if (scope) {
                    s.append (L"%");
                    s.append (std::to_wstring (scope)); // GetAdaptersAddresses IP_ADAPTER_ADDRESSES.AdapterName
                }
            }
            return s;
        }
    }
}

std::wstring raddi::log::translate (const std::tm & t, const std::wstring & format) {
    wchar_t string [24];
    _snwprintf (string, sizeof string / sizeof string [0],
                format.empty () ? L"%04u-%02u-%02u %02u:%02u:%02u" : format.c_str (),
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return string;
}

#ifdef _WIN32
std::wstring raddi::log::translate (const SOCKADDR_INET & address, const std::wstring & format) {
    return translate (reinterpret_cast <const sockaddr *> (&address), format);
}

std::wstring raddi::log::translate (const FILETIME & utc, const std::wstring & format) {
    SYSTEMTIME st;
    if (FileTimeToSystemTime (&utc, &st)) {
        return translate (st, format);
    } else
        return L"";
}
std::wstring raddi::log::translate (const SYSTEMTIME & st, const std::wstring & format) {
    wchar_t string [24];
    _snwprintf (string, sizeof string / sizeof string [0],
                format.empty () ? L"%04u-%02u-%02u %02u:%02u:%02u.%03u" : format.c_str (),
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return string;
}

std::wstring raddi::log::translate (const GUID & guid, const std::wstring &) {
    wchar_t sz [40] = { 0 };
    StringFromGUID2 (guid, sz, 40);
    return sz;
}
#endif

raddi::log::api_error::api_error () : code (GetLastError ()) {};
