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
    DWORD               message = 0;

public:
    lock                lock;
    raddi::instance *   instance = nullptr;
    raddi::db *         database = nullptr;

public:
    Node () : provider ("connection") {};

    // initialize
    //  - thread calling this will receive 'message' callback
    //  - 'instance' specifies PID of concrete instance the user may want to use
    //
    bool initialize (const wchar_t * instance, DWORD message);
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

    template <unsigned int>
    static void db_table_change_notify (void *);
};

#endif
