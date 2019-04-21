#ifndef NODE_H
#define NODE_H

#include "../core/raddi.h"

// Node
//  - node connection for client app
//  - singleton, mutiple instances 
//
class Node {
    const wchar_t *     parameter;
    raddi::instance *   instance = nullptr;// (option (argc, argw, L"instance"));
    raddi::db *         database = nullptr;// (file::access::read,

public:
    bool initialize (const wchar_t * instance);
    void terminate ();

    bool connected () const;

private:
    long worker ();
};

#endif
