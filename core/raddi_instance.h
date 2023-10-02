#ifndef RADDI_INSTANCE_H
#define RADDI_INSTANCE_H

#include <windows.h>
#include <string>
#include <map>
#include <set>
#include "..\common\log.h"
#include "..\common\uuid.h"

namespace raddi {

    // instance
    //  - abstracts platform-dependent storage of per-instance status info
    //
    class instance {
        HKEY registry = NULL;
        HKEY overview = NULL;
        HKEY connections = NULL;

        public:
            wchar_t pid [14];

            // status/failure_point
            //  - set on failures, to be logged later
            //
            LONG status = ERROR_SUCCESS;
            const wchar_t * failure_point = nullptr;

        public:
            // instance (scope)
            //  - creates information store (registry key) for current NODE instance (PID)
            //  - NODE instance store per-PID is volatile, data deleted on logoff/reboot
            // 
            explicit instance (raddi::log::scope);

            // instance (uuid)
            //  - connects to information store (registry key) for current APP instance (uuid)
            //  - APP instance data are not deleted on logoff/reboot
            // 
            // explicit instance (uuid app);

            // instance (pid)
            //  - searches and connects to 'pid' instance or first available (if 'pid' is nullptr)
            //  - the connection is read-only, set functions will fail
            // 
            explicit instance (const wchar_t * pid);

            // destructor
            //  - deletes the information store iff created in the constructor
            //
            ~instance ();

            // set
            //  - 
            //
            bool set (const wchar_t * name, const std::wstring & value);
            bool set (const wchar_t * name, const std::string & value);
            bool set (const wchar_t * name, const wchar_t * value);
            bool set (const wchar_t * name, const char * value);
            bool set (const wchar_t * name, unsigned long long value);
            bool set (const wchar_t * name, unsigned long value);
            bool set (const wchar_t * name, unsigned int value);
            bool set (const wchar_t * name, FILETIME value);
            bool set (const wchar_t * name, uuid value);

            bool report_begin ();
            void report_connection (const std::wstring & name, const std::wstring & value);
            void report_finish ();

            // get
            //  - 
            //
            template <typename T> T get (const wchar_t * name) const;

            // description
            //  - 
            //
            struct description {
                DWORD session = 0;
                bool  running = false; // false = crashed
                bool  broadcasting = false;
                unsigned char priority = 0; // lower value = higher priority
            };

            // enumerate
            //  - lists running and crashed instances on the current system
            //  - map key is PID
            //
            static std::map <unsigned int, description> enumerate ();

            // clean
            //  - TODO: deletes all registry entries that remain after crashed processes
            //
            static std::size_t clean ();

        private:
            bool set (const wchar_t * name, DWORD type, const void *, std::size_t);
            bool get (const wchar_t * name, DWORD type, BYTE *, DWORD *) const;

            HKEY find_pid (HKEY hBase);
            HKEY find_sub (HKEY hBase);
            
            static void enum_pids (HKEY hBase, std::map <unsigned int, description> &, unsigned char priority);

            instance (const instance &) = delete;
            instance & operator = (const instance &) = delete;

            std::set <std::wstring> report_set;
    };
}

template <> uuid               raddi::instance::get <uuid>               (const wchar_t *) const;
template <> FILETIME           raddi::instance::get <FILETIME>           (const wchar_t *) const;
template <> unsigned int       raddi::instance::get <unsigned int>       (const wchar_t *) const;
template <> unsigned long      raddi::instance::get <unsigned long>      (const wchar_t *) const;
template <> unsigned long long raddi::instance::get <unsigned long long> (const wchar_t *) const;

template <> std::wstring       raddi::instance::get <std::wstring>       (const wchar_t *) const;

#endif
