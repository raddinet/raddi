#include "server.h"
#include "../common/platform.h"

#include <ws2tcpip.h>
#include <mstcpip.h>

#include <cstring>
#include <bitset>

#pragma warning (disable:6250) // VirtualFree, decommit without release

namespace {
    LPFN_ACCEPTEX ptrAcceptEx = NULL;
    LPFN_GETACCEPTEXSOCKADDRS ptrGetAcceptExSockAddrs = NULL;
    LPFN_CONNECTEX ptrConnectEx = NULL;
    LPFN_DISCONNECTEX ptrDisconnectEx = NULL;
    LPFN_TRANSMITFILE ptrTransmitFile = NULL;
    LPFN_TRANSMITPACKETS ptrTransmitPackets = NULL;
    
    class Bitmap {
        std::uint8_t *      memory = nullptr;
        std::bitset <8192>  map; // TODO: dynamically add another arenas
        lock                lock;
        static const auto   GRANULARITY = 0x10000; // fixed MM property since WinNT 3.5 to 10.0.16299, so far
        
    public:
        bool initialize (std::size_t n) noexcept {
            if ((n > 0) && (n <= this->map.size ())) {
                this->memory = (std::uint8_t *) VirtualAlloc (NULL, this->map.size () * GRANULARITY, MEM_RESERVE, PAGE_NOACCESS);
            }
            return this->memory;
        }

        std::uint8_t * allocate () noexcept {
            if (this->memory) {
                exclusive guard (this->lock);

                for (auto i = 0u; i != this->map.size (); ++i)
                    if (this->map [i] == false) {
                        auto p = this->memory + i * GRANULARITY;
                        if (VirtualAlloc (p, GRANULARITY, MEM_COMMIT, PAGE_READWRITE)) {
                            this->map.set (i, true);
                            return p;
                        }
                    }
                
                return nullptr;
            } else
                return (std::uint8_t *) VirtualAlloc (NULL, GRANULARITY, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        }
        void release (std::uint8_t * p) noexcept {
            if (p) {
                if (this->memory) {
                    exclusive guard (this->lock);
                    this->map.set ((p - this->memory) / GRANULARITY, false);
                } else
                    VirtualFree (p, 0, MEM_RELEASE);
            }
        }
        void compact () noexcept {
            exclusive guard (this->lock);
            for (auto i = 0u; i != this->map.size (); ++i)
                if (this->map [i] == false) {
                    VirtualFree (this->memory + (i * GRANULARITY), GRANULARITY, MEM_DECOMMIT);
                }
        }
    } bitmap;

    template <typename T>
    void WSAGetPtr (SOCKET s, GUID guid, T & ptr, const wchar_t * name) noexcept {
        DWORD n = 0;
        if (WSAIoctl (s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof guid, &ptr, sizeof ptr, &n, NULL, NULL) != 0 || ptr == nullptr) {
            raddi::log::stop (raddi::component::main, 5, guid, name);
        }
    }
}

bool WSAInitialize () noexcept {
    WSADATA wsa;
    if (WSAStartup (0x0202, &wsa) == 0 && LOWORD (wsa.wVersion) == 0x0202) {
        SOCKET s = socket (AF_INET, SOCK_STREAM, 0);
        if (s != INVALID_SOCKET) {

            // TODO: these seems to be missing on ReactOS
            WSAGetPtr (s, WSAID_ACCEPTEX, ptrAcceptEx, L"AcceptEx");
            WSAGetPtr (s, WSAID_GETACCEPTEXSOCKADDRS, ptrGetAcceptExSockAddrs, L"GetAcceptExSockAddrs");
            WSAGetPtr (s, WSAID_CONNECTEX, ptrConnectEx, L"ConnectEx");
            
            // not used, yet
            WSAGetPtr (s, WSAID_DISCONNECTEX, ptrDisconnectEx, L"DisconnectEx");
            WSAGetPtr (s, WSAID_TRANSMITFILE, ptrTransmitFile, L"TransmitFile");
            WSAGetPtr (s, WSAID_TRANSMITPACKETS, ptrTransmitPackets, L"TransmitPackets");

            closesocket (s);
            if (ptrAcceptEx && ptrGetAcceptExSockAddrs && ptrConnectEx)
                return true;
        }
    }
    return false;
}

bool ReserveServerMemory (std::size_t n) noexcept {
    return bitmap.initialize (n);
}
void CompactServerMemory () noexcept {
    bitmap.compact ();
}

bool StringToAddress (SOCKADDR_INET & address, const wchar_t * string) noexcept {
    std::memset (&address, 0, sizeof address);
    INT length = sizeof address;
    return WSAStringToAddress (const_cast <wchar_t *> (string), AF_INET6, NULL, reinterpret_cast <SOCKADDR *> (&address.Ipv6), &length) == 0
        || WSAStringToAddress (const_cast <wchar_t *> (string), AF_INET, NULL, reinterpret_cast <SOCKADDR *> (&address.Ipv4), &length) == 0;
}

namespace {
    std::wstring make_instance_name (short family, std::uint16_t port) {
        wchar_t string [24];
        switch (family) {
            case AF_INET:
                _snwprintf (string, sizeof string / sizeof string [0], L"IPv%u:%u", 4u, port);
                break;
            case AF_INET6:
                _snwprintf (string, sizeof string / sizeof string [0], L"IPv%u:%u", 6u, port);
                break;
            default:
                _snwprintf (string, sizeof string / sizeof string [0], L"%u:%u", family, port);
                break;
        }
        return string;
    }
    std::wstring make_instance_name (const SOCKADDR_INET & address) {
        return raddi::log::translate (address, std::wstring ());
    }
}

// static data members live here

counter Receiver::total;
counter Transmitter::total;
Listener::Totals Listener::total;
UdpPoint::Totals UdpPoint::total;

// Overlapped

void Overlapped::cancel (HANDLE h) noexcept {
    if (Optional <BOOL, HANDLE, LPOVERLAPPED> (L"KERNEL32", "CancelIoEx", h, this)) {
        this->Internal = STATUS_ABANDONED_WAIT_0;
    }
}

// Socket

Socket::Socket (int family, int type, int protocol)
    : s (WSASocket (family, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED)) {

    if (this->s == INVALID_SOCKET)
        throw raddi::log::exception (raddi::component::server, 1, family, type, protocol);
}
Socket::Socket (Socket && from) noexcept
    : s (from.s) {

    from.s = INVALID_SOCKET;
}
Socket & Socket::operator = (Socket && from) noexcept {
    this->disconnect ();
    std::swap (this->s, from.s);
    return *this;
}
Socket::~Socket () noexcept {
    this->disconnect ();
}
void Socket::disconnect () noexcept {
    if (this->s != INVALID_SOCKET) {
        closesocket (this->s); // TODO: DisconnectEx?
        this->s = INVALID_SOCKET;
    }
}

// Receiver

Receiver::Receiver (Socket && s)
    : Socket (std::move (s))
    , buffer (bitmap.allocate ()) {}

Receiver::~Receiver () {
    bitmap.release (this->buffer);
}

bool Receiver::accepted () {
    if (this->buffer != nullptr) {
        this->connecting = false;
        return this->await (*this)
            && this->connected ()
            && this->next ();
    } else {
        this->connecting = false;
        this->overloaded ();
        return false;
    }
}

bool Receiver::next (std::uint16_t o) {
    this->offset = o;

    DWORD flags = 0;
    WSABUF wsabuf = {
        (ULONG) (65536 - o),
        (char *) &this->buffer [0] + o
    };
    return WSARecv (*this, &wsabuf, 1, NULL, &flags, this, NULL) == 0
        || GetLastError () == ERROR_IO_PENDING
        || this->report (raddi::log::level::error, 4);
}

void Receiver::completion (bool success, std::size_t n) {
    if (success) {
        if (this->connecting) {
            this->connecting = false;

            if (this->connected ())
                if (this->next ())
                    return;
        } else {
            this->counter += n;
            this->total += n;
            if (n) {
                n += this->offset;

            more:
                std::size_t size = n;
                if (this->inbound (&this->buffer [0], size)) {
                    if (size == n) {
                        if (this->next ())
                            return;
                    }
                    if (size > n) {
                        if (size < (1u << (CHAR_BIT * sizeof (this->offset)))) {
                            if (this->next ((std::uint16_t) n))
                                return;
                        } else
                            this->report (raddi::log::level::error, 0xA1F0, L"internal error"); // TODO, this catches 'offset' overflow
                    }
                    if (size < n) {
                        // TODO: walk the buffer, don't pop-front until the last item
                        //       no need to align, further 'decode' call decodes to aligned buffer
                        std::memmove (&this->buffer [0], &this->buffer [size], 65536 - size); // pop front
                        n -= size;
                        goto more;
                    }
                }
            }
        }
    }
    this->disconnected ();
}

// Transmitter

Transmitter::Transmitter (Socket && s)
    : Socket (std::move (s)) {

    this->pending.reserve (65536); // raddi::protocol::max_frame_size
}

Transmitter::~Transmitter () {}

void Transmitter::optimize () {
    exclusive guard (this->lock);
    this->awaiting.shrink_to_fit ();
    if (this->pending.empty () && this->pending.capacity () > 65536) {
        this->pending.shrink_to_fit ();
        this->pending.reserve (65536);
    }
}

bool Transmitter::send (std::vector <unsigned char> & data) {
    if (data.empty ())
        return true;

    WSABUF wsabuf = { (unsigned long) data.size (), (char *) data.data () };
    return WSASend (*this, &wsabuf, 1, NULL, 0, this, NULL) == 0
        || GetLastError () == ERROR_IO_PENDING
        || this->report (raddi::log::level::error, 5, data.size ());
}

unsigned char * Transmitter::prepare (std::size_t size) {
    if ((SOCKET) *this != INVALID_SOCKET) {
        try {
            if (this->pending.empty ()) {
                this->pending.resize (size);
                return this->pending.data ();
            } else {
                const auto offset = this->awaiting.size ();
                this->awaiting.resize (offset + size);
                return this->awaiting.data () + offset;
            }
        } catch (const std::bad_alloc &) {
            this->counters.oom += size;
        }
    }
    return nullptr;
}

bool Transmitter::transmit (const unsigned char * data, std::size_t size) {
    try {
        if (data == this->pending.data ()) {
            if (size > this->pending.size ()) {
                this->report (raddi::log::level::stop, 0xF0, size, this->pending.size ());
            }

            this->pending.resize (size);
            if (this->send (this->pending)) {
                return true;

            } else {
                this->counters.dropped += size;
                this->pending.clear ();
                return false;
            }
        } else {
            const auto offset = data - this->awaiting.data ();

            if (offset + size > this->awaiting.size ()) {
                this->report (raddi::log::level::stop, 0xF0, size, this->awaiting.size () - offset);
            }

            this->awaiting.resize (offset + size);
            this->counters.delayed += size;
            return true;
        }
    } catch (const std::bad_alloc &) {
        this->counters.oom += size;
        return false;
    }
}

void Transmitter::completion (bool success, std::size_t n) {
    if (success) {
        this->total += n;
        this->counters.sent += n;

        exclusive guard (this->lock);
        this->pending.clear ();
        if (!this->awaiting.empty ()) {
            if (this->send (this->awaiting)) {
                this->pending.swap (this->awaiting);
            } else {
                this->counters.dropped += n;
            }
        }
    } else {
        this->counters.dropped += n;
        this->report (raddi::log::level::error, 6, n);
    }
}

// Connection

Connection::Connection (Socket && s) noexcept
    : Socket (std::move (s))
    , Receiver (std::move (s))
    , Transmitter (std::move (s)) {}

Connection::Connection (ADDRESS_FAMILY family)
    : Socket (family, SOCK_STREAM, IPPROTO_TCP)
    , Receiver (Socket (family, SOCK_STREAM, IPPROTO_TCP))
    , Transmitter (Socket (family, SOCK_STREAM, IPPROTO_TCP)) {

    SOCKADDR_INET placeholder;
    std::memset (&placeholder, 0, sizeof placeholder);
    placeholder.si_family = family;

    if (bind (*this, reinterpret_cast <const sockaddr *> (&placeholder), sizeof placeholder) != 0)
        throw raddi::log::exception (raddi::component::server, 2, (unsigned int) family);
}

bool Connection::connect (const SOCKADDR_INET & peer) {
    if (this->Receiver::start ()) {
        return ptrConnectEx (*this, reinterpret_cast <const sockaddr *> (&peer), sizeof peer, NULL, 0, NULL, (Receiver *) this)
            || GetLastError () == ERROR_IO_PENDING
            || this->report (raddi::log::level::error, 3, peer);
    } else
        return false;
}
void Connection::terminate () noexcept {
    shutdown (*this, SD_SEND);
    this->Transmitter::cancel ((HANDLE) (SOCKET) *this);
    this->Receiver::cancel ((HANDLE) (SOCKET) *this);
    this->Socket::disconnect ();
}

// Listener

Listener::Listener (short family, const std::wstring & instance)
    : provider ("listener", instance)
    , listener (family, SOCK_STREAM, IPPROTO_TCP)
    , prepared (family, SOCK_STREAM, IPPROTO_TCP)
    , family (family) {}

Listener::Listener (short family, std::uint16_t port)
    : Listener (family, make_instance_name (family, port)) {

    const int enabled = 1;
    setsockopt (this->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast <const char *> (&enabled), sizeof enabled);

    SOCKADDR_INET address;
    std::memset (&address, 0, sizeof address);

    address.si_family = family;
    switch (family) {
        case AF_INET:
            address.Ipv4.sin_port = htons (port);
            break;
        case AF_INET6:
            address.Ipv6.sin6_port = htons (port);
            break;
    }
    if (bind (this->listener, reinterpret_cast <const sockaddr *> (&address), sizeof address) != 0)
        throw raddi::log::exception (raddi::component::server, 7, address);
}

Listener::Listener (const SOCKADDR_INET & address)
    : Listener (address.si_family, make_instance_name (address)) {

    if (bind (this->listener, reinterpret_cast <const sockaddr *> (&address), sizeof address) != 0)
        throw raddi::log::exception (raddi::component::server, 7, address);
}

bool Listener::start () {
    if (this->family == AF_INET6) {
        const int enabled = 1;
        setsockopt (this->listener, IPPROTO_IPV6, IPV6_V6ONLY,
                    reinterpret_cast <const char *> (&enabled), sizeof enabled);
    }
    if ((listen (this->listener, SOMAXCONN) == 0) && (this->await (this->listener))) {
        return this->next ();
    } else {
        return this->report (raddi::log::level::error, 8);
    }
}

void Listener::stop () noexcept {
    // TODO: allow restarting
    exclusive guard (this->lock);
    this->listener.disconnect ();
    this->prepared.disconnect ();
}

bool Listener::next () {
    DWORD n = 0;
    return ptrAcceptEx (this->listener, this->prepared, buffer, 0, 44, 44, &n, this)
        || GetLastError () == ERROR_IO_PENDING
        || GetLastError () == WSAECONNRESET // TODO: call ptrAcceptEx again in this case?
        || this->report (raddi::log::level::error, 9);
}

void Listener::completion (bool success, std::size_t) {
    exclusive guard (this->lock);
    if (this->listener != INVALID_SOCKET) {

        if (success) {
            sockaddr * local;
            sockaddr * remote;
            int localsize;
            int remotesize;

            ptrGetAcceptExSockAddrs (this->buffer, 0, 44, 44, &local, &localsize, &remote, &remotesize);

            if (this->connected (local, remote)) {
                ++this->accepted;
                ++this->total.accepted;
            } else {
                ++this->rejected;
                ++this->total.rejected;
            }
            this->prepared = Socket (this->family, SOCK_STREAM, IPPROTO_TCP);
        } else {
            ++this->rejected;
            ++this->total.rejected;
        }
        this->next ();
    }
}

// UdpPoint

UdpPoint::UdpPoint (short family, std::uint16_t port, const std::wstring & instance)
    : Socket (family, SOCK_DGRAM, IPPROTO_UDP)
    , family (family)
    , port (port) {

    const int enabled = 1;
    setsockopt (*this, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast <const char *> (&enabled), sizeof (enabled));
}

UdpPoint::UdpPoint (short family, std::uint16_t port)
    : UdpPoint (family, port, make_instance_name (family, port)) {
    
    SOCKADDR_INET address;
    std::memset (&address, 0, sizeof address);

    switch (family) {
        case AF_INET:
            address.Ipv4.sin_family = family;
            address.Ipv4.sin_port = htons (port);
            address.Ipv4.sin_addr.s_addr = INADDR_ANY;
            break;
        case AF_INET6:
            address.Ipv6.sin6_family = family;
            address.Ipv6.sin6_port = htons (port);

            const int enabled = 1;
            setsockopt (*this, IPPROTO_IPV6, IPV6_V6ONLY,
                        reinterpret_cast <const char *> (&enabled), sizeof enabled);
            break;
    }
    if (bind (*this, reinterpret_cast <const sockaddr *> (&address), sizeof address) != 0)
        throw raddi::log::exception (raddi::component::server, 7, address);
}

UdpPoint::UdpPoint (const SOCKADDR_INET & address)
    : UdpPoint (address.si_family, ntohs (address.Ipv4.sin_port), make_instance_name (address)) {

    if (bind (*this, reinterpret_cast <const sockaddr *> (&address), sizeof address) != 0)
        throw raddi::log::exception (raddi::component::server, 7, address);
}

bool UdpPoint::start () {
    if ((this->buffer != nullptr) && (this->await (*this))) {
        return this->next ();
    } else
        return false;
}

void UdpPoint::stop () noexcept {
    // TODO: allow restarting
    exclusive guard (this->lock);
    this->disconnect ();
}

bool UdpPoint::next () {
    DWORD n = 0;
    DWORD flags = 0u;
    WSABUF wsabuf = { sizeof this->buffer, (char *) this->buffer };

    this->from_size = sizeof this->from;
    return WSARecvFrom (*this, &wsabuf, 1u, NULL, &flags,
                        reinterpret_cast <sockaddr *> (&this->from), &this->from_size, this, NULL) == 0
        || WSAGetLastError () == WSA_IO_PENDING
        || this->report (raddi::log::level::error, 9);
}

void UdpPoint::completion (bool success, std::size_t n) {
    exclusive guard (this->lock);
    if (success && (*this != INVALID_SOCKET)) {
        this->packet (this->buffer, n, reinterpret_cast <sockaddr *> (&this->from), this->from_size);
        this->received += n;
        this->total.received += n;
        this->next ();
    }
}

bool UdpPoint::send (const void * data, std::size_t size, const sockaddr * to, int to_len) {
    DWORD n;
    WSABUF wsabuf = { (unsigned long) size, (char *) data };
    return WSASendTo (*this, &wsabuf, 1, &n, 0, to, to_len, NULL, NULL) == 0
        || this->report (raddi::log::level::error, 13);
}

bool UdpPoint::enable_broadcast () {
    const int hops = 3;
    const int enabled = 1;
    const int disabled = 0;

    switch (this->family) {
        case AF_INET:
            return setsockopt (*this, IPPROTO_IP, IP_MULTICAST_LOOP,
                               reinterpret_cast <const char *> (&disabled), sizeof disabled) == 0
                && setsockopt (*this, SOL_SOCKET, SO_BROADCAST,
                               reinterpret_cast <const char *> (&enabled), sizeof enabled) == 0;
        case AF_INET6:
            ipv6_mreq membership;
            std::memset (&membership, 0, sizeof membership);

            membership.ipv6mr_multiaddr.u.Word [0] = 0x15ff;
            membership.ipv6mr_multiaddr.u.Word [7] = 0x0001;

            return setsockopt (*this, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                               reinterpret_cast <const char *> (&hops), sizeof hops) == 0
                && setsockopt (*this, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                               reinterpret_cast <const char *> (&membership), sizeof membership) == 0;
    }
    return false;
}

bool UdpPoint::broadcast (const void * data, std::size_t size, std::uint16_t port) {
    SOCKADDR_INET address;
    std::memset (&address, 0, sizeof address);

    switch (this->family) {
        case AF_INET:
            address.Ipv4.sin_family = AF_INET;
            address.Ipv4.sin_port = htons (port);
            address.Ipv4.sin_addr.s_addr = INADDR_BROADCAST;
            return this->send (data, size,
                               reinterpret_cast <const sockaddr *> (&address.Ipv4), sizeof address.Ipv4);
        case AF_INET6:
            address.Ipv6.sin6_family = AF_INET6;
            address.Ipv6.sin6_port = htons (port);
            address.Ipv6.sin6_addr.u.Word [0] = 0x15ff;
            address.Ipv6.sin6_addr.u.Word [7] = 0x0001;
            return this->send (data, size,
                               reinterpret_cast <const sockaddr *> (&address.Ipv6), sizeof address.Ipv6);// */
    }
    return false;
}

