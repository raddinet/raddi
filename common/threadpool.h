#ifndef RADDI_THREADPOOL_H
#define RADDI_THREADPOOL_H

#include <windows.h>
#include <cstddef>

// threadpool
//  - simple threadpool controller, separating platform specific code
//  - TODO: sprinkle with raddi::log::note so we see what is going on?
//
template <typename Thread>
class threadpool {
    HANDLE      handle;
    std::size_t workload;
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

    void init (std::size_t workload);
    void begin ();
    bool dispatch (void (Thread::*fn)(), Thread * t, bool defer);
    void join ();
};

#include "threadpool.tcc"
#endif

