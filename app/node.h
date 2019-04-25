#ifndef NODE_H
#define NODE_H

#include "../core/raddi.h"
#include "../common/log.h"

// Node
//  - node connection for client app
//  - singleton, mutiple instances will break!
//  - thread that initialized the instance get
//
class Node
    : virtual raddi::log::provider <raddi::component::main> {

    raddi::instance *   instance = nullptr; // (option (argc, argw, L"instance"));
    raddi::db *         database = nullptr; // (file::access::read,
    const wchar_t *     parameter = nullptr;
    DWORD               guiThreadId = 0;
    DWORD               guiMessage = 0;

public:
    Node () : provider ("connection") {};

    bool initialize (const wchar_t * instance, DWORD message);
    void terminate ();

    bool connected () const;

private:
    long worker () noexcept;
    bool disconnect ();
    bool reconnect ();
};

#endif
