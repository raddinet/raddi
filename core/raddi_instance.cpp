#include "raddi_instance.h"
#include "raddi_protocol.h"
#include "raddi_timestamp.h"
#include "../common/platform.h"
#include <VersionHelpers.h>
#include <cstdio>
#include <cwchar>

raddi::instance::instance (raddi::log::scope scope) {
    this->owner = true;

    _snwprintf (this->pid, sizeof this->pid / sizeof this->pid [0], L"%u", GetCurrentProcessId ());
    this->status = RegCreateKeyEx ((scope == raddi::log::scope::machine) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                                   L"SOFTWARE\\RADDI.net", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &this->registry, NULL);
    if (this->status != ERROR_SUCCESS) {
        this->failure_point = L"SOFTWARE\\RADDI.net";
    } else {
        this->status = RegCreateKeyEx (this->registry, this->pid, 0, NULL, REG_OPTION_VOLATILE, KEY_READ | KEY_WRITE, NULL, &this->overview, NULL);
        if (this->status != ERROR_SUCCESS) {
            this->failure_point = this->pid;
        } else {
            FILETIME ftStart, ftExit, ftKernel, ftUser;
            if (GetProcessTimes (GetCurrentProcess (), &ftStart, &ftExit, &ftKernel, &ftUser)) {
                this->set (L"start", ftStart);
            } else {
                this->status = GetLastError ();
                this->failure_point = L"GetProcessTimes";
            }
            RegCreateKeyEx (this->overview, L"connections", 0, NULL, REG_OPTION_VOLATILE, KEY_READ | KEY_WRITE, NULL, &this->connections, NULL);
        }
    }
    SetLastError (this->status);
}

/*raddi::instance::instance (uuid app) {
    this->pid [0] = L'\0';
    this->status = RegCreateKeyEx (HKEY_CURRENT_USER,
                                   L"SOFTWARE\\RADDI.net", 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &this->registry, NULL);
    if (this->status != ERROR_SUCCESS) {
        this->failure_point = L"SOFTWARE\\RADDI.net";
    } else {
        this->status = RegCreateKeyEx (registry, app.c_wstr ().c_str (), 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &this->overview, NULL);
        if (this->status != ERROR_SUCCESS) {
            this->failure_point = L"uuid";
        }
    }
    SetLastError (this->status);
}// */

bool raddi::instance::set (const wchar_t * name, uuid value) {
    return this->set (name, REG_BINARY, &value, sizeof value);
}
bool raddi::instance::set (const wchar_t * name, FILETIME value) {
    return this->set (name, REG_QWORD, &value, sizeof (FILETIME));
}
bool raddi::instance::set (const wchar_t * name, unsigned long long value) {
    return this->set (name, REG_QWORD, &value, sizeof (unsigned long long));
}
bool raddi::instance::set (const wchar_t * name, unsigned long value) {
    return this->set (name, REG_DWORD, &value, sizeof (unsigned long));
}
bool raddi::instance::set (const wchar_t * name, unsigned int value) {
    return this->set (name, REG_DWORD, &value, sizeof (unsigned int));
}
bool raddi::instance::set (const wchar_t * name, const wchar_t * value) {
    return this->set (name, REG_SZ, value, sizeof (wchar_t) * (std::wcslen (value) + 1));
}
bool raddi::instance::set (const wchar_t * name, const char * string) {
    std::wstring s;
    if (string) {
        auto n = MultiByteToWideChar (CP_ACP, 0, string, -1, NULL, 0) - 1;
        if (n > 0) {
            s.resize (n);
            if (MultiByteToWideChar (CP_ACP, 0, string, -1, &s [0], n + 1) > 0) {
                return this->set (name, s);
            }
        }
    }
    return false;
}
bool raddi::instance::set (const wchar_t * name, const std::wstring & value) {
    return this->set (name, REG_SZ, value.data (), sizeof (wchar_t) * (value.size () + 1));
}
bool raddi::instance::set (const wchar_t * name, const std::string & value) {
    return this->set (name, value.c_str ());
}
bool raddi::instance::set (const wchar_t * name, DWORD type, const void * data, std::size_t size) {
    if (this->overview) {
        return RegSetValueEx (this->overview, name, NULL, type, reinterpret_cast <const BYTE *> (data), (DWORD) size) == ERROR_SUCCESS;
    } else
        return false;
}

bool raddi::instance::report_begin () {
    return this->connections != NULL;
}
void raddi::instance::report_connection (const std::wstring & name, const std::wstring & value) {
    auto size = (DWORD) (sizeof (wchar_t) * (value.size () + 1));
    if (RegSetValueEx (this->connections, name.c_str (), NULL, REG_SZ, reinterpret_cast <const BYTE *> (value.data ()), size) == ERROR_SUCCESS) {
        this->report_set.insert (name);
    }
}
void raddi::instance::report_finish () {
    DWORD deleted;
    do {
        deleted = 0;

        DWORD i = 0;
        wchar_t name [256]; // 16384

        DWORD n = sizeof name / sizeof name [0];
        while (RegEnumValue (this->connections, i++, name, &n, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            n = sizeof name / sizeof name [0];

            if (!this->report_set.count (name)) {
                if (RegDeleteValue (this->connections, name) == ERROR_SUCCESS) {
                    ++deleted;
                    --i;
                }
            }
        }
    } while (deleted);
    this->report_set.clear ();
}

template <> uuid raddi::instance::get <uuid> (const wchar_t * name) const {
    uuid data;
    DWORD size = sizeof data;
    if (this->get (name, REG_BINARY, reinterpret_cast <BYTE *> (&data), &size)) {
        return data;
    } else {
        data.null ();
        return data;
    }
}
template <> FILETIME raddi::instance::get <FILETIME> (const wchar_t * name) const {
    FILETIME data;
    DWORD size = sizeof data;
    if (this->get (name, REG_QWORD, reinterpret_cast <BYTE *> (&data), &size)) {
        return data;
    } else
        return { 0, 0 };
}
template <> unsigned int raddi::instance::get <unsigned int> (const wchar_t * name) const {
    return this->get <unsigned long> (name);
}
template <> unsigned long raddi::instance::get <unsigned long> (const wchar_t * name) const {
    DWORD data;
    DWORD size = sizeof data;
    if (this->get (name, REG_DWORD, reinterpret_cast <BYTE *> (&data), &size)) {
        return data;
    } else
        return 0;
}
template <> unsigned long long raddi::instance::get <unsigned long long> (const wchar_t * name) const {
    unsigned long long data;
    DWORD size = sizeof data;
    if (this->get (name, REG_QWORD, reinterpret_cast <BYTE *> (&data), &size)) {
        return data;
    } else
        return 0;
}

template <> std::wstring raddi::instance::get <std::wstring> (const wchar_t * name) const {
    DWORD size = 0;
    if (this->get (name, REG_SZ, nullptr, &size)) {
        std::wstring result;
        result.resize (size / sizeof (wchar_t));
        if (this->get (name, REG_SZ, reinterpret_cast <BYTE *> (&result [0]), &size)) {
            result.pop_back ();
            return result;
        }
    }
    return std::wstring ();
}
bool raddi::instance::get (const wchar_t * name, DWORD type, BYTE * data, DWORD * size) const {
    if (this->overview) {
        DWORD realtype;
        if (RegQueryValueEx (this->overview, name, NULL, &realtype, data, size) == ERROR_SUCCESS) {
            return realtype == type
                || (realtype == REG_DWORD && type == REG_QWORD);
        }
    }
    return false;
}

raddi::instance::instance (const wchar_t * parameter) {
    this->owner = false;

    if (parameter != nullptr) {
        _snwprintf (this->pid, sizeof this->pid / sizeof this->pid [0], L"%s", parameter);
    } else {
        this->pid [0] = L'\0';
    }

    if (parameter == nullptr) {
        this->overview = this->find_sub (HKEY_CURRENT_USER);
        if (this->overview)
            return;
    }

    this->overview = this->find_sub (HKEY_LOCAL_MACHINE);
    if (this->overview)
        return;

    DWORD i = 0;
    wchar_t user [256];

    DWORD n = sizeof user / sizeof user [0];
    while (RegEnumKeyEx (HKEY_USERS, i++, user, &n, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        n = sizeof user / sizeof user [0];

        HKEY hUser;
        if (RegOpenKeyEx (HKEY_USERS, user, 0, KEY_READ, &hUser) == ERROR_SUCCESS) {
            this->overview = this->find_sub (hUser);
            RegCloseKey (hUser);
            if (this->overview)
                return;
        }
    }

    this->status = ERROR_FILE_NOT_FOUND;
    SetLastError (this->status);
}

HKEY raddi::instance::find_pid (HKEY hBase) {
    HKEY hInstance = NULL;
    DWORD i = 0;

    wchar_t sub [40];
    DWORD n = sizeof sub / sizeof sub [0];
    while (RegEnumKeyEx (hBase, i++, sub, &n, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        n = sizeof sub / sizeof sub [0];

        // is this the instance we are searching for? iff we are searching for one, that is

        if (this->pid [0]) {
            if (std::wcscmp (sub, this->pid) != 0)
                continue;
        }

        // is this APP uuid?

        if (std::wcschr (sub, L'-'))
            continue;

        if (RegOpenKeyEx (hBase, sub, 0, KEY_READ, &hInstance) == ERROR_SUCCESS) {
            std::wcsncpy (this->pid, sub, sizeof this->pid / sizeof this->pid [0]);

            // validate heartbeat
            //  - if the heartbeat wasn't recently updated, we consider the instance crashed
            //  - TODO: completely rewrite to first enum all instances, then select the best

            std::uint64_t heartbeat = 0;
            DWORD size = sizeof heartbeat;

            if ((RegQueryValueEx (hInstance, L"heartbeat", NULL, NULL, (BYTE *) &heartbeat, &size) == ERROR_SUCCESS)
             && (raddi::microtimestamp () - heartbeat < 30'000'000)) {

                return hInstance;

            } else {
                RegCloseKey (hInstance);
                continue;
            }
        }
    }
    return NULL;
}
HKEY raddi::instance::find_sub (HKEY hBase) {
    HKEY hKey;
    if (RegOpenKeyEx (hBase, L"SOFTWARE\\RADDI.net", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (auto hInstance = this->find_pid (hKey)) {
            RegCloseKey (hKey);
            return hInstance;
        }
        RegCloseKey (hKey);
    }
    return NULL;
}

raddi::instance::~instance () {
    if (this->connections) {
        RegCloseKey (this->connections);
        this->connections = NULL;
    }
    if (this->overview) {
        if (this->owner) {
            RegDeleteKey (this->overview, L"connections");
        }
        RegCloseKey (this->overview);
        this->overview = NULL;
    }
    if (this->registry) {
        if (this->owner) {
            RegDeleteKey (this->registry, this->pid);
        }
        RegCloseKey (this->registry);
        this->registry = NULL;
    }
}

std::map <unsigned int, raddi::instance::description> raddi::instance::enumerate () {
    std::map <unsigned int, description> result;

    enum_pids (HKEY_CURRENT_USER, result, 0);
    enum_pids (HKEY_LOCAL_MACHINE, result, 3);
    
    wchar_t user [256];

    DWORD i = 0;
    DWORD n = sizeof user / sizeof user [0];
    while (RegEnumKeyEx (HKEY_USERS, i++, user, &n, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        n = sizeof user / sizeof user [0];

        HKEY hUser;
        if (RegOpenKeyEx (HKEY_USERS, user, 0, KEY_READ, &hUser) == ERROR_SUCCESS) {
            enum_pids (hUser, result, 6);
            RegCloseKey (hUser);
        }
    }
    return result;
}

void raddi::instance::enum_pids (HKEY hBase, std::map <unsigned int, description> & results, unsigned char priority) {
    HKEY hKey;
    if (RegOpenKeyEx (hBase, L"SOFTWARE\\RADDI.net", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        wchar_t process [40];

        DWORD i = 0;
        DWORD n = sizeof process / sizeof process [0];
        while (RegEnumKeyEx (hKey, i++, process, &n, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            n = sizeof process / sizeof process [0];

            // app uuid

            if (std::wcschr (process, L'-'))
                continue;

            DWORD size;
            DWORD type;
            HKEY hInstance;

            if (RegOpenKeyEx (hKey, process, 0, KEY_READ, &hInstance) == ERROR_SUCCESS) {
                auto pid = std::wcstoul (process, nullptr, 10);

                description instance;
                instance.priority = priority;

                if (!GetProcessSessionId (pid, &instance.session)) {
                    // instance is probably inaccessible
                    instance.priority += 9;
                }
                
                // validate magic
                //  - to distinguish from different running nodes, where this code was used in different software

                char magic [12] = {};
                size = sizeof magic;

                if (RegQueryValueExA (hInstance, "magic", NULL, &type, (BYTE *) magic, &size) != ERROR_SUCCESS
                    || type != REG_SZ
                    || size > sizeof raddi::protocol::magic
                    || std::strncmp (magic, raddi::protocol::magic, sizeof raddi::protocol::magic) != 0) {

                    RegCloseKey (hInstance);
                    continue;
                }

                // validate heartbeat
                //  - if the heartbeat wasn't recently updated, we consider the instance crashed

                std::uint64_t heartbeat = 0;
                size = sizeof heartbeat;

                if (RegQueryValueEx (hInstance, L"heartbeat", NULL, NULL, (BYTE *) &heartbeat, &size) == ERROR_SUCCESS) {
                    if (raddi::microtimestamp () - heartbeat < 30'000'000) {
                        instance.running = true;
                    } else {
                        instance.priority += 10;
                    }
                }

                DWORD value = 0;
                size = sizeof value;

                if (RegQueryValueEx (hInstance, L"broadcasting", NULL, NULL, (BYTE *) &value, &size) == ERROR_SUCCESS) {
                    instance.broadcasting = !!value;
                    instance.priority += !instance.broadcasting;
                }

                try {
                    results.try_emplace (pid, instance);
                    RegCloseKey (hInstance);
                } catch (...) {
                    RegCloseKey (hInstance);
                    RegCloseKey (hKey);
                    throw;
                }
            }
        }
        RegCloseKey (hKey);
    }
}
