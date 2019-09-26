#ifndef NODE_H
#define NODE_H

#include "../core/raddi.h"
#include "../common/log.h"
#include "../common/lock.h"

// Node
//  - node connection for client app
//  - singleton, mutiple instances will break!
//
class Node
    : virtual raddi::log::provider <raddi::component::main> {

    const wchar_t *     parameter = nullptr;
    DWORD               status = 0;
    DWORD               message = 0;

public:
    lock                lock;
    raddi::instance *   instance = nullptr;
    raddi::db *         database = nullptr;

    // table
    //  - lParam identifying changed table 
    //
    enum class table : unsigned int {
        data = 0,
        threads = 1,
        channels = 2,
        identities = 3,
    };

public:
    Node () : provider ("connection") {};

    // initialize
    //  - thread calling this will receive 'status' and 'message'+0 to 'message'+3 callbacks
    //  - 'instance' specifies PID of concrete instance the user may want to use
    //
    bool initialize (const wchar_t * instance, DWORD status, DWORD message);
    void terminate ();

    // start
    //  - actually starts connecting (in background thread)
    //
    void start ();

    // connected 
    //  - quick query to see if we are connected to a node software
    //
    bool connected () const noexcept;

private:
    long worker () noexcept;
    bool disconnect () noexcept;
    bool reconnect () noexcept;

    template <enum table>
    static void db_table_change_notify (void *, std::uint32_t, std::uint32_t);
};

#endif
