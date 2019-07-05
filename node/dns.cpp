#include "dns.h"

Dns::Dns ()
    : provider ("DNS")
    , thread (CreateThread (NULL, 0, WorkerFwd, this, CREATE_SUSPENDED, NULL)) {

    this->events [0] = CreateEvent (NULL, FALSE, FALSE, NULL);
    this->events [1] = CreateEvent (NULL, TRUE, FALSE, NULL);

    if (this->thread && this->events [0] && this->events [1]) {
        ResumeThread (this->thread);
    } else {
        if (this->events [0]) CloseHandle (this->events [0]);
        if (this->events [1]) CloseHandle (this->events [1]);
        throw raddi::log::exception (raddi::component::main, 10);
    }
}

Dns::~Dns () {
    SetEvent (this->events [1]);
    WaitForSingleObject (this->thread, INFINITE);

    CloseHandle (this->thread);
    CloseHandle (this->events [0]);
    CloseHandle (this->events [1]);
}

bool Dns::resolve (Recipient * recipient, wchar_t * uri, unsigned short port) {
    std::wstring original = uri;
    Dns::Type type = Dns::Type::A;

    // skip dns protocol prefix
    if (std::wcsncmp (uri, L"dns:", 4) == 0) {
        uri += 4;
    }

    // skip authority, if any; this will keep authority as a domain to query if // is used by mistake
    if (std::wcsncmp (uri, L"//", 2) == 0) {
        if (auto rslash = std::wcsrchr (uri, L'/')) {
            uri = rslash + 1;
        }
    }

    // remove fragment (should not really be there)
    if (auto hash = std::wcschr (uri, L'#')) {
        *hash = L'\0';
    }

    // find 'type=xxx' parameter after question mark
    if (auto qmark = std::wcschr (uri, L'?')) {
        *qmark = L'\0';

        // lowercase all
        for (auto p = qmark + 1; *p; ++p) {
            *p = std::tolower (*p);
        }

        // iterate through parameters, zero-terminate current before evaluating
        wchar_t * next = qmark + 1;
        do {
            auto parameter = next;

            next = std::wcschr (next, L';');
            if (next) {
                *next++ = L'\0';
            }

            if (std::wcsncmp (parameter, L"type=", 5) == 0) {
                parameter += 5;

                if (std::wcscmp (parameter, L"a") == 0) {
                    type = Dns::Type::A;
                    break;
                } else
                if (std::wcscmp (parameter, L"txt") == 0) {
                    type = Dns::Type::TXT;
                    break;
                } else
                if (std::wcscmp (parameter, L"aaaa") == 0) {
                    type = Dns::Type::AAAA;
                    break;
                } else {
                    this->report (raddi::log::level::error, 0x2A, original, parameter);
                    return false;
                }
            }
        } while (next);
    }

    // extension, parse port number to use
    if (auto colon = std::wcschr (uri, L':')) {
        *colon = L'\0';
        if (auto uriport = std::wcstoul (colon + 1, nullptr, 10)) {
            if ((uriport != 0) && (uriport < 65536)) {
                port = (unsigned short) uriport;
            } else {
                this->report (raddi::log::level::error, 0x29, original, colon + 1, port);
            }
        } else {
            this->report (raddi::log::level::error, 0x29, original, colon + 1, port);
        }
    }

    if (uri [0]) {
        this->resolve (recipient, type, uri, port);
        return true;
    } else
        return false;
}

void Dns::resolve (Recipient * recipient, Type type, const wchar_t * domain, unsigned short port) {
    {
        exclusive guard (this->lock);
        this->requests.push_back ({ type, port, domain, recipient });
    }
    SetEvent (this->events [0]);
}

void Dns::worker () {
    while (WaitForMultipleObjects (2, this->events, FALSE, INFINITE) == WAIT_OBJECT_0) {

        this->lock.acquire_exclusive ();
        auto n = this->requests.size ();
        this->lock.release_exclusive ();

        for (std::size_t i = 0; i != n; ++i) {
            const auto & request = this->requests [i];

            DNS_RECORD * results = NULL;
            auto result = DnsQuery_W (request.name.c_str (), (WORD) request.type,
                                      DNS_QUERY_BYPASS_CACHE, NULL, &results, NULL);

            if (result == ERROR_SUCCESS) {
                try {
                    DNS_RECORD * record = results;
                    do {
                        switch (record->wType) {

                            // case DNS_TYPE_CNAME:
                            //    request.recipient->resolved (request.name, record->Data.Cname.pNameHost);
                            //    break;
                            case DNS_TYPE_TEXT:
                                for (auto j = 0u; j != record->Data.TXT.dwStringCount; ++j) {
                                    request.recipient->resolved (request.name, record->Data.TXT.pStringArray [j]);
                                }
                                break;

                            case DNS_TYPE_A:
                            case DNS_TYPE_AAAA:

                                SOCKADDR_INET address;
                                std::memset (&address, 0, sizeof address);

                                switch (record->wType) {
                                    case DNS_TYPE_A:
                                        address.si_family = AF_INET;
                                        address.Ipv4.sin_port = htons (request.port);
                                        address.Ipv4.sin_addr.S_un.S_addr = record->Data.A.IpAddress;
                                        break;

                                    case DNS_TYPE_AAAA:
                                        address.si_family = AF_INET6;
                                        address.Ipv6.sin6_port = htons (request.port);
                                        std::memcpy (address.Ipv6.sin6_addr.u.Byte, record->Data.AAAA.Ip6Address.IP6Byte, 16);
                                        break;
                                }

                                request.recipient->resolved (request.name, address);
                                break;
                        }
                    } while ((record = record->pNext) != NULL);

                } catch (const std::bad_alloc & x) {
                    raddi::log::error (5, this->identity.instance, GetCurrentThreadId (), x.what ());
                } catch (const std::exception & x) {
                    raddi::log::error (6, this->identity.instance, GetCurrentThreadId (), x.what ());
                } catch (...) {
                    raddi::log::stop (7, this->identity.instance, GetCurrentThreadId ());
                }

                DnsRecordListFree (results, DnsFreeRecordList);
            } else {
                if (result == DNS_INFO_NO_RECORDS) {
                    this->report (raddi::log::level::data, 0x28, request.name, request.type);
                } else {
                    this->report (raddi::log::level::error, 0x28, request.name, request.type, raddi::log::api_error { ( unsigned long) result });
                }
            }
        }

        this->lock.acquire_exclusive ();
        this->requests.erase (this->requests.begin (), this->requests.begin () + n);
        this->lock.release_exclusive ();
    }
}

DWORD WINAPI Dns::WorkerFwd (LPVOID lpVoid) {
    reinterpret_cast <Dns *> (lpVoid)->worker ();
    return 0;
}