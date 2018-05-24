#ifndef RADDI_INSTANCE_H
#define RADDI_INSTANCE_H

#include <windows.h>
#include <string>

namespace raddi {

    // instance
    //  - abstracts platform-dependent storage of per-instance status info
    //
    class instance {
        HKEY registry = NULL;
        HKEY overview = NULL;
        HKEY commands = NULL;

        public:
            wchar_t pid [14];

            // status/failure_point
            //  - 
            //
            LONG status = ERROR_SUCCESS;
            const wchar_t * failure_point = nullptr;

        public:
            // instance (bool)
            //  - creates information store (registry key) for current
            //    instance (PID) either in HKLM (true) or HKCU (false)
            // 
            explicit instance (bool global);

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
            bool set (const wchar_t * name, const wchar_t * value);
            bool set (const wchar_t * name, unsigned long long value);
            bool set (const wchar_t * name, unsigned long value);
            bool set (const wchar_t * name, unsigned int value);
            bool set (const wchar_t * name, FILETIME value);

            // get
            //  - 
            //
            template <typename T> T get (const wchar_t * name) const;

            // enumerate
            //  - 
            //
            static bool enumerate ();

            // clean
            //  - TODO: deletes all registry entries that remained after crashed processes
            //
            static std::size_t clean ();

        private:
            bool set (const wchar_t * name, DWORD type, const void *, std::size_t);
            bool get (const wchar_t * name, DWORD type, BYTE *, DWORD *) const;

            HKEY find_pid (HKEY hBase);
            HKEY find_sub (HKEY hBase);

            instance (const instance &) = delete;
            instance & operator = (const instance &) = delete;
    };
}

template <> FILETIME           raddi::instance::get <FILETIME>           (const wchar_t *) const;
template <> unsigned int       raddi::instance::get <unsigned int>       (const wchar_t *) const;
template <> unsigned long      raddi::instance::get <unsigned long>      (const wchar_t *) const;
template <> unsigned long long raddi::instance::get <unsigned long long> (const wchar_t *) const;

template <> std::wstring raddi::instance::get <std::wstring> (const wchar_t *) const;

#endif
