#ifndef RESOLVER_H
#define RESOLVER_H

#include "../core/raddi.h"
#include "../common/log.h"
#include "../common/lock.h"

#include <set>

// Resolver
//  - background priority thread that resolves names for identities or channels
//    as they might have been changed during the existence
//  - uses global 'Node connection'
//
class Resolver
    : virtual raddi::log::provider <raddi::component::main> {

    lock  lock;
    DWORD message = 0;

    HANDLE thread = NULL;
    HANDLE finish = NULL;
    HANDLE update = NULL;

    std::uint32_t top = 0;

    //std::set <raddi::eid> once;
    //std::map <raddi::eid, std::wstring> resolved; // ???

public:
    Resolver () : provider ("resolver") {};

    // initialize
    //  - thread calling this will receive 'message' callback
    //
    bool initialize (DWORD message);
    void terminate ();

    // start
    //  - actually starts resolving (in background thread)
    //
    void start ();


    // TODO: resolve once
    // TODO: resolve and keep resolving on changes (forward from gui thread???)
    // TODO: priority?

    void add (const raddi::eid &);

    // resolve
    //  - 
    //
    void resolve (raddi::eid);

    // evaluate
    //  - called by application when 'threads' table changes (contains also name-change entries)
    //
    void evaluate ();

private:
    long worker () noexcept;
};

#endif