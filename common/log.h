#ifndef RADDI_LOG_H
#define RADDI_LOG_H

#ifndef RC_INVOKED
#include <winsock2.h>
#include <ws2tcpip.h>
#include <guiddef.h>
#endif

// TODO: rewrite:
//  - remove note..error levels (let callsite decide importance)
//  - add 'object' distinction (below component) for classes (or merge with MAIN,SERVER,SOURCE,...)
//     - remove named components, just use code

#undef ERROR
#define NOTE  0x0A000
#define EVENT 0x0C000
#define DATA  0x0D000
#define ERROR 0x0E000
#define STOP  0x0F000

#define MAIN 0x00100
#define SERVER 0x00200
#define SOURCE 0x00300
#define DATABASE 0x00400
// #define COORDINATOR 0x00500

#ifndef RC_INVOKED
#include <type_traits>
#include <stdexcept>
#include <cstdarg>
#include <string>

namespace raddi {

    // component
    //  - list of basic components of the software to simplify tracking of message IDs
    //
    enum class component : short {
        main = MAIN,
        server = SERVER,
        source = SOURCE,
        database = DATABASE,
    };
    
    namespace log {
        
        // level
        //  - importance of reported event
        //
        enum class level : int {
            all   = 0,
            note  = NOTE,   // standard behavior notifications
            event = EVENT,  // significant runtime events
            data  = DATA,   // malformed/invalid data processed
            error = ERROR,  // recoverable (reducing functionality) runtime errors
            stop  = STOP,   // irrecoverable errors
            disabled = 0xFFFFF
        };

        namespace settings {
            extern log::level level;
            extern log::level display;
        }

        // initialize/display
        //  - parses string to determine log path and level
        //    for log file (initialize) or console output (display)
        //
        bool initialize (const wchar_t *, const wchar_t * subdir, const wchar_t * prefix, bool service);
        void display (const wchar_t *);

        // path
        //  - whenever 'initialize' succeeds then 'path' contains full path to the log file
        //
        extern wchar_t path [768];

        // api_error
        //  - adds type information to GetLastError
        //  - TODO: use std::error_category etc?
        //
        struct api_error {
            unsigned long code;
        public:
            api_error (); // code (GetLastError ())
            api_error (unsigned long code) : code (code) {};
        };

        // translate
        //  - converts log function argument to string, according to second 'format' parameter
        //  - provide overload to extend functionality
        //  - TODO: since we now use ADL figure out better name that won't be confusing in global namespace
        //
        inline std::wstring translate (long long argument, const std::wstring & format) {
            return std::to_wstring (argument);
        }
        inline std::wstring translate (unsigned long long argument, const std::wstring & format) {
            wchar_t buffer [64];
            if (format == L"x") std::swprintf (buffer, sizeof buffer / sizeof buffer [0], L"%llx", argument); else
            if (format == L"X") std::swprintf (buffer, sizeof buffer / sizeof buffer [0], L"%llX", argument); else
            std::swprintf (buffer, sizeof buffer / sizeof buffer [0], L"%llu", argument);
            return buffer;
        }
        inline std::wstring translate (long argument, const std::wstring & format) { return translate ((long long) argument, format); }
        inline std::wstring translate (unsigned long argument, const std::wstring & format) { return translate ((unsigned long long) argument, format); }
        inline std::wstring translate (int argument, const std::wstring & format) { return translate ((long long) argument, format); }
        inline std::wstring translate (unsigned int argument, const std::wstring & format) { return translate ((unsigned long long) argument, format); }

        inline std::wstring translate (const std::wstring & argument, const std::wstring &) { return argument; }
        inline std::wstring translate (const wchar_t * argument, const std::wstring &) { return argument; }
        inline std::wstring translate (std::nullptr_t argument, const std::wstring &) { return L""; }

        std::wstring translate (api_error, const std::wstring &);
        std::wstring translate (const in_addr *, const std::wstring &);
        std::wstring translate (const in6_addr *, const std::wstring &);
        std::wstring translate (const sockaddr *, const std::wstring &);

        inline std::wstring translate (const in_addr & address, const std::wstring & format) { return translate (&address, format); }
        inline std::wstring translate (const in6_addr & address, const std::wstring & format) { return translate (&address, format); }
        inline std::wstring translate (const sockaddr & address, const std::wstring & format) { return translate (&address, format); }

#ifdef _WIN32
        // TODO: replace with 'std::tm' where needed to be portable
        std::wstring translate (GUID, const std::wstring &);
        std::wstring translate (FILETIME, const std::wstring &);
        std::wstring translate (SYSTEMTIME, const std::wstring &);
        std::wstring translate (SOCKADDR_INET, const std::wstring &);
#endif

        std::wstring translate (const void * argument, const std::wstring &);
        std::wstring translate (const char * argument, const std::wstring &);
        std::wstring translate (const std::string & argument, const std::wstring &);

        // internal
        //  - implementation details
        //
        namespace internal {
            struct provider_name {
                std::wstring instance; // TODO: use something with less overhead?
                const char * object = nullptr;
            };

            bool evaluate_level (level l);
            std::wstring load_string (component c, level l, unsigned int id);
            void deliver (component c, level, const provider_name *, unsigned int, std::wstring);

            inline
            void capture_overriden_api_error (api_error &) {}

            template <typename T>
            void capture_overriden_api_error (api_error & e, T) {
                capture_overriden_api_error (e);
            }
            template <typename T, typename... Args>
            void capture_overriden_api_error (api_error & e, T, Args ...remaining) {
                capture_overriden_api_error (e, remaining...);
            }
            template <typename... Args>
            void capture_overriden_api_error (api_error & e, api_error ee, Args ...remaining) {
                e = ee;
                capture_overriden_api_error (e, remaining...);
            }

            template <typename T>
            void replace_argument (std::wstring & string, std::wstring prefix, T argument) {

                std::wstring::size_type offset = 0u;
                while ((offset = string.find (L"{" + prefix, offset)) != std::wstring::npos) {

                    auto fo = prefix.size () + offset + 1;
                    auto fe = string.find (L'}', fo);

                    if (fe == std::wstring::npos)
                        break;

                    if (std::is_same <std::nullptr_t, T>::value) {

                        string.replace (offset, fe + 1 - offset, std::wstring ());
                    } else {

                        auto translated = translate (argument, fo != fe ? string.substr (fo + 1, fe - fo - 1) : std::wstring ());
                        if (prefix != L"ERR") {
                            translated = L"\x201C" + translated + L"\x201D";
                        }

                        string.replace (offset, fe + 1 - offset, translated);
                        offset += translated.size ();
                    }
                }
            }
            inline
            void replace_arguments (std::wstring &, std::size_t) {}

            template <typename T, typename... Args>
            void replace_arguments (std::wstring & string, std::size_t total, T argument, Args ...remaining) {
                replace_arguments (string, total, remaining...);
                replace_argument (string, std::to_wstring (total - sizeof... (remaining)), argument);
            }
        }

        // provider
        //  - virtually inheritable class to prerecord identity information for logging purposes
        //  - allows errors to be reported as: return this->report (log::level::error, 123, ...);
        //  - "\x0018" marks moved-from instance whose destruction is not reported
        //
        template <component c>
        class provider : internal::provider_name {
        public:
            template <std::size_t N>
            provider (const char (&object) [N], const std::wstring & instance = std::wstring ())
                : provider_name { instance, instance != L"\x0018" ? object : nullptr } {

                if (this->instance != L"\x0018") {
                    this->report (level::note, 0xA1F1);
                }
            }
            provider () {
                this->report (level::note, 0xA1F1);
            }
            provider (provider && from)
                : internal::provider_name (from) {
                
                from.instance = L"\x0018";
            }
            /*provider & operator = (provider && from) {
                this->internal::provider_name::operator = (from);
                from.instance = L"\x0018";
                return *this;
            }*/
            ~provider () {
                if (this->instance != L"\x0018") {
                    this->report (level::note, 0xA1F2);
                }
            }

            template <typename... Args>
            bool report (level l, unsigned int id, Args... args) const;
        };

        // report
        //  - primary reporting
        //  - argument {ERR} is always API error (GetLastError ()), last by default,
        //    but can be overriden by passing instance of api_error
        //  - always returns false so it can be used within negative return statement
        //
        template <typename... Args>
        bool report (component c, level ll, const internal::provider_name * p, unsigned int id, Args... args) {
            if (internal::evaluate_level (ll)) {

                api_error error;
                auto string = internal::load_string (c, ll, id);

                internal::capture_overriden_api_error (error, args...);
                internal::replace_arguments (string, sizeof... (args), args...);
                internal::replace_argument (string, L"ERR", error);
                internal::deliver (c, ll, p, id, std::move (string));
            }
            // always return false!
            return false;
        }

        template <typename... Args>
        bool report (component c, level l, unsigned int id, Args... args) {
            return report (c, l, nullptr, id, args...);
        }

        template <component c>
        template <typename... Args>
        bool provider <c> ::report (level l, unsigned int id, Args... args) const {
            return raddi::log::report (c, l, this, id, args...);
        }

        // exception
        //  - 
        //
        class exception : public std::exception {
        public:
            template <typename... Args>
            exception (component c, unsigned int id, Args... args) {
                raddi::log::report (c, raddi::log::level::error, id, args...);
            }
        };

        // note/event/data/error/stop
        //  - shortcuts to 'report' particular condition
        //
        template <typename... Args>
        bool note (component c, unsigned int id, Args... args) {
            return report (c, raddi::log::level::note, id, args...);
        }
        template <typename... Args>
        bool event (component c, unsigned int id, Args... args) {
            return report (c, raddi::log::level::event, id, args...);
        }
        template <typename... Args>
        bool data (component c, unsigned int id, Args... args) {
            return report (c, raddi::log::level::data, id, args...);
        }
        template <typename... Args>
        bool error (component c, unsigned int id, Args... args) {
            return report (c, raddi::log::level::error, id, args...);
        }
        template <typename... Args>
        bool stop (component c, unsigned int id, Args... args) {
            return report (c, raddi::log::level::stop, id, args...);
        }
        template <typename... Args>
        bool note (unsigned int id, Args... args) {
            return report (raddi::component::main, raddi::log::level::note, id, args...);
        }
        template <typename... Args>
        bool event (unsigned int id, Args... args) {
            return report (raddi::component::main, raddi::log::level::event, id, args...);
        }
        template <typename... Args>
        bool data (unsigned int id, Args... args) {
            return report (raddi::component::main, raddi::log::level::data, id, args...);
        }
        template <typename... Args>
        bool error (unsigned int id, Args... args) {
            return report (raddi::component::main, raddi::log::level::error, id, args...);
        }
        template <typename... Args>
        bool stop (unsigned int id, Args... args) {
            return report (raddi::component::main, raddi::log::level::stop, id, args...);
        }
    }
}

#undef NOTE
#undef EVENT
#undef DATA
#undef ERROR
#undef STOP

#undef MAIN
#undef SERVER
#undef SOURCE
#undef DATABASE

#endif
#endif
