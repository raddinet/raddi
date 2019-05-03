#ifndef NODE_H
#define NODE_H

#include "../core/raddi.h"
#include "../common/log.h"
#include "../common/lock.h"

// Node
//  - node connection for client app
//  - singleton, mutiple instances will break!
//  - thread that initialized the instance get
//
class Node
    : virtual raddi::log::provider <raddi::component::main> {

    lock                lock;
    raddi::instance *   instance = nullptr; // (option (argc, argw, L"instance"));
    raddi::db *         database = nullptr;
    const wchar_t *     parameter = nullptr;
    DWORD               guiThreadId = 0;
    DWORD               guiMessage = 0;

public:
    Node () : provider ("connection") {};

    bool initialize (const wchar_t * instance, DWORD message);
    void terminate ();

    bool connected () const noexcept;

private:
    long worker () noexcept;
    bool disconnect () noexcept;
    bool reconnect () noexcept;

    template <unsigned int>
    static void db_table_change_notify (void *);
};

#endif
