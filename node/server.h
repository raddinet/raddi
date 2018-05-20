#ifndef RADDI_SERVER_H
#define RADDI_SERVER_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <cwchar>
#include <vector>

#include "sodium.h"
#include "../common/lock.h"
#include "../common/log.h"

bool WSAInitialize () noexcept;
bool StringToAddress (SOCKADDR_INET & address, const wchar_t * string) noexcept;

// ReserveServerMemory
//  - reserves slots for N receivers
//  - value of 0 or higher than internal maximum (1024 now) disables reservation
//    and allows unlimited connections, note that that is order of magnitudes slower (for now)
//
bool ReserveServerMemory (std::size_t n) noexcept;

// CompactServerMemory
//  - decommits unused memory slots
//
void CompactServerMemory () noexcept;

class Socket {
    SOCKET s;
public:
    Socket (int family, int type, int protocol);
    Socket (Socket && from) noexcept;
    Socket & operator = (Socket && from);
    ~Socket () noexcept;

    void disconnect () noexcept;
    operator SOCKET () const noexcept { return this->s; }
};

class Overlapped : public OVERLAPPED {
public:
    Overlapped () noexcept {
        this->Internal = ERROR_SUCCESS;
        this->hEvent = NULL;
    }
    bool await (HANDLE h, void * key = nullptr) noexcept;
    bool await (Socket & s) noexcept {
        return this->await ((HANDLE) (SOCKET) s, &s);
    }
    bool enqueue () noexcept;
    bool pending () const noexcept {
        return !HasOverlappedIoCompleted (this);
    }
    void cancel (HANDLE h) noexcept;
    void wait (HANDLE h) noexcept {
        DWORD n;
        GetOverlappedResult (h, this, &n, TRUE);
    }
    void wait (Socket & s) noexcept {
        return this->wait ((HANDLE) (SOCKET) s);
    }

    virtual void completion (bool success, std::size_t n) = 0;
};

struct Counter {
    unsigned long long n = 0;
    unsigned long long bytes = 0;

    void operator += (std::size_t value) noexcept {
        InterlockedAdd64 ((volatile LONG64 *) &this->bytes, value);
        InterlockedIncrement (&this->n);
    }
};

class Transmitter
    : public Overlapped
    , virtual Socket
    , virtual raddi::log::provider <raddi::component::server> {

    std::vector <unsigned char> pending;
    std::vector <unsigned char> awaiting;

    void completion (bool success, std::size_t n) override;
    bool send (std::vector <unsigned char> &);

public:
    Transmitter (Socket &&);
    virtual ~Transmitter () = 0;

    // optimize
    //  - releases as much memory as possible
    //  - requires synchronization thus don't request optimization for non-secured connections
    //
    void optimize ();

    // transmit
    //  - exclusive lock, prepare, fill with data, transmit
    //  - it's not necessary to lock on first transmit, i.e. inside 'connected' or 'overloaded'
    //
    mutable lock lock;
    unsigned char * prepare (std::size_t size);
    bool transmit (const unsigned char * prepared, std::size_t size);

    bool unsynchronized_is_live () const noexcept {
        return !this->awaiting.empty ()
            && !this->pending.empty ();
    }

    /*std::size_t buffered () const {
        return this->awaiting.size ();
    }*/

public:
    struct {
        Counter delayed;
        Counter dropped;
        Counter sent;
        Counter oom;
    } counters;

    static Counter total;
};

class Receiver
    : public Overlapped
    , virtual Socket
    , virtual raddi::log::provider <raddi::component::server> {

    std::uint8_t *  buffer = nullptr;
    std::uint16_t   offset = 0;
protected:
    bool            connecting = true;

private:
    void completion (bool success, std::size_t n) override;
    bool next (std::uint16_t offset = 0);
    
    // inbound
    //  - returning false results in connection disconnecting
    //  - on return, set 'size' to:
    //     - bytes that were processed (less or equal to size)
    //     - or that are required to have full packet (greate than size)
    //     - zero to get called again
    //
    virtual bool inbound (const unsigned char * data, std::size_t & size) = 0;
    virtual bool connected () = 0;
    virtual void overloaded () = 0;
    virtual void disconnected () = 0;

protected:
    Receiver (Socket &&);
    ~Receiver ();

    // start
    //  - enqueues the receiver to thread-pool
    //  - must be called well after constructor chain finishes due to presence
    //    of virtual and pure virtual functions that may be invoked immediately
    //
    bool start () {
        return this->await (*this);
    }

    // accepted
    //  - call back to parent ('connected') to transmit our headers and
    //    start the receiver for accepted connection (data are inbound)
    //
    bool accepted ();

    // counter
    //  - number of fragments and total size of received data on the socket
    //
    Counter counter;

public:
    static Counter total;
};

class Connection
    : virtual protected Socket
    , virtual protected raddi::log::provider <raddi::component::server>
    , protected Receiver
    , protected Transmitter {

protected:
    explicit Connection (Socket &&) noexcept;
    explicit Connection (ADDRESS_FAMILY);

    bool pending () const noexcept {
        return this->Receiver::Overlapped::pending ()
            || this->Transmitter::Overlapped::pending ();
    }
    bool connect (const SOCKADDR_INET & peer);
    void terminate () noexcept;
};

class Listener
    : public Overlapped
    , private raddi::log::provider <raddi::component::server> {

    Socket  listener;
    Socket  prepared;
    lock    lock;
    short   family;
    char    buffer [88];

    Listener (short family, const std::wstring &);

    bool next ();
    bool connected (sockaddr * local, sockaddr * remote);
    void completion (bool success, std::size_t n) override;

public:
    Listener (short family, std::uint16_t port);
    Listener (const SOCKADDR_INET & address);

    std::size_t accepted = 0;
    std::size_t rejected = 0;

    bool start ();
    void stop () noexcept;

public:
    static struct Totals {
        std::size_t accepted = 0;
        std::size_t rejected = 0;
    } total;
};

class UdpPoint
    : public Overlapped
    , protected Socket
    , virtual protected raddi::log::provider <raddi::component::server> {

    lock            lock;
    SOCKADDR_INET   from;
    INT             from_size;
    std::uint8_t    buffer [16];

    UdpPoint (short family, std::uint16_t port, const std::wstring &);

    bool next ();
    void completion (bool success, std::size_t n) override;

    virtual void packet (std::uint8_t * data, std::size_t size, sockaddr * from, int) = 0;

protected:
    short           family;
    std::uint16_t   port;

public:
    UdpPoint (short family, std::uint16_t port);
    UdpPoint (const SOCKADDR_INET & address);

    bool start ();
    void stop () noexcept;
    
    bool enable_broadcast ();
    bool send (const void * data, std::size_t size, const sockaddr * to, int to_len);
    bool broadcast (const void * data, std::size_t size, std::uint16_t port);

    Counter received;
    Counter sent;

public:
    static struct Totals {
        Counter received;
        Counter sent;
    } total;
};

// translate
//  - for passing Counters as a log function parameter
//
inline std::wstring translate (Counter c, const std::wstring &) {
    static const char prefix [] = { 'B', 'k', 'M', 'G', 'T', 'P', 'E' };

    auto m = 0;
    auto v = (double) c.bytes;
    while (v >= 922) {
        v /= 1024.0;
        ++m;
    }
    wchar_t number [64];
    std::swprintf (number, sizeof number / sizeof number [0], L"%llu (%.*f %c%s)",
                    c.n, (v < 10.0 && c.bytes > 10), v, prefix [m], (m != 0) ? L"B" : L"");
    return number;
}

#endif
