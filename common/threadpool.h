#ifndef RADDI_THREADPOOL_H
#define RADDI_THREADPOOL_H

#include <windows.h>

// threadpool
//  - simple threadpool controller, separating platform specific code
//  - TODO: sprinkle with raddi::log::note so we see what is going on?
//
template <typename Thread>
class threadpool {
    HANDLE      handle;
    std::size_t semaphore;

    struct parameter {
        threadpool *    pool;
        Thread *        thread;
        void (Thread::* function) ();
    };
    static DWORD WINAPI ThreadProc (LPVOID);

public:
    threadpool ();
    ~threadpool ();

    void begin (std::size_t);
    bool dispatch (void (Thread::*fn)(), Thread * t);
    void join ();
};

#include "threadpool.tcc"
#endif

