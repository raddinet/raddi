#ifndef RADDI_THREADPOOL2_H
#define RADDI_THREADPOOL2_H

#include <windows.h>
#include <cstddef>
#include <vector>

#include "lock.h"
#include "platform.h"

// threadpool2
//  - alternative threadpool with full control of core affinity
//  - supports multiple processor group spanning
//
template <typename Fiber>
class threadpool2 {
    HANDLE  semaphore;
    HANDLE  done;

    BOOL    quit = FALSE;
    LONG    consumed = 0;
    LONG    deferred = 0;
    LONG    remains = 0;
    LONG    threads = 0;

    struct workitem {
        Fiber *        fiber;
        void (Fiber::* function) ();
    };
    std::vector <workitem>  queue;

private:
    static DWORD WINAPI ThreadProcFwd (LPVOID);

    DWORD thread ();

public:
    threadpool2 ();
    ~threadpool2 ();

    bool init (std::size_t workload);
    void begin ();
    bool dispatch (void (Fiber::*fn)(), Fiber *, bool defer);
    void join ();
    void stop ();
};

#include "threadpool2.tcc"
#endif

