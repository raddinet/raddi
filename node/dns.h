#ifndef DNS_H
#define DNS_H

#include <Windows.h>
#include <WinDNS.h>

#include "../common/log.h"
#include "../common/lock.h"

#include <vector>
#include <string>

class Dns
    : raddi::log::provider <raddi::component::main> {

    HANDLE thread = NULL;
    HANDLE events [2] = { NULL, NULL }; // trigger/terminate
    
public:
    Dns ();
    ~Dns ();

    enum class Type : WORD {
        A = DNS_TYPE_A,
        TXT = DNS_TYPE_TEXT,
        AAAA = DNS_TYPE_AAAA,
    };

    // Recipient
    //  - classes, that need to receive asynchronously retrieved DNS records, implement this interface
    //
    class Recipient {
    public:
        virtual void resolved (const std::wstring &, const SOCKADDR_INET & address) {};
        virtual void resolved (const std::wstring &, const std::wstring & text) {};
    };

    // resolve
    //  - starts asynchronous retrieval of DNS record
    //  - WARNING: the action is destructive on the string provided in 'uri' !!!
    //  - [dns:][//authority/]domain.com[:port][?type=<A|AAAA|TXT>]
    //     - authority is ignored, as is # fragment part (if any)
    //     - if no port is specified, default_port is used; for TXT records the port is ignored
    //        - 'port' is our non-standard extension to RFC 4501
    //  - returns: true if parsed correctly and queried
    //             false on bad format, error is reported to log
    //
    bool resolve (Recipient * recipient, wchar_t * uri, unsigned short default_port);

    // resolve
    //  - starts asynchronous retrieval of DNS record
    //  - 'domain' is used as-is, 'port' is used to construct SOCKADDR_INET for A or AAAA records
    //
    void resolve (Recipient * recipient, Type type, const wchar_t * domain, unsigned short port);

private:
    struct Request {
        Type            type;
        unsigned short  port;
        std::wstring    name;
        Recipient *     recipient;
    };

    lock                  lock;
    std::vector <Request> requests;
    void                  worker ();

    static DWORD WINAPI WorkerFwd (LPVOID);
};

// translate
//  - for passing Dns::Type as a log function parameter
//
inline std::wstring translate (enum class Dns::Type type, const std::wstring &) {
    switch (type) {
        case Dns::Type::A: return L"A";
        case Dns::Type::TXT: return L"TXT";
        case Dns::Type::AAAA: return L"AAAA";
    }
    return std::to_wstring (( WORD) type);
}

#endif
