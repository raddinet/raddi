#ifndef RADDI_THREADPOOL2_TCC
#define RADDI_THREADPOOL2_TCC

template <typename Fiber>
threadpool2 <Fiber> ::threadpool2 ()
    : semaphore (CreateSemaphore (NULL, 0, 0x7FFF'FFFF, NULL))
    , done (CreateEvent (NULL, FALSE, FALSE, NULL)) {}

template <typename Fiber>
threadpool2 <Fiber> ::~threadpool2 () {
    this->stop ();
    CloseHandle (this->semaphore);
    CloseHandle (this->done);
}

template <typename Fiber>
bool threadpool2 <Fiber> ::init (std::size_t workload) {
    if (this->threads) {
        this->stop ();
    }
    this->queue.reserve (workload);
    this->quit = false;

    auto processors = GetRankedLogicalProcessorList ();
    if (workload > processors.size ()) {
        workload = processors.size ();
    }

    for (std::size_t i = 0; i != workload; ++i) {
        auto handle = CreateThread (NULL, 256 * 1024, ThreadProcFwd,
                                    this, CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (handle) {
            InterlockedIncrement (&this->threads);
            AssignThreadLogicalProcessor (handle, processors [i % processors.size ()]);
            ResumeThread (handle);
            CloseHandle (handle);
        } else {
            this->stop ();
            return false;
        }
    }
    return true;
}

template <typename Fiber>
void threadpool2 <Fiber> ::stop () {
    this->quit = true;
    ReleaseSemaphore (this->semaphore, this->threads, NULL);
    WaitForSingleObject (this->done, INFINITE);
}

template <typename Fiber>
void threadpool2 <Fiber> ::begin () {
    this->queue.clear ();
    this->consumed = 0;
}

template <typename Fiber>
bool threadpool2 <Fiber> ::dispatch (void (Fiber::*fn)(), Fiber * ctx, bool defer) {
    this->queue.push_back ({ ctx, fn });
    InterlockedIncrement (&this->remains);

    if (defer) {
        InterlockedIncrement (&this->deferred);
    } else {
        ReleaseSemaphore (this->semaphore, InterlockedExchange (&this->deferred, 0) + 1, NULL);
    }
    return true;
}

template <typename Fiber>
void threadpool2 <Fiber> ::join () {
    if (auto release = InterlockedExchange (&this->deferred, 0)) {
        ReleaseSemaphore (this->semaphore, release, NULL);
    }
    WaitForSingleObject (this->done, INFINITE);
}

template <typename Fiber>
DWORD WINAPI threadpool2 <Fiber> ::ThreadProcFwd (LPVOID this_) {
    return static_cast <threadpool2 *> (this_)->thread ();
}

template <typename Fiber>
DWORD threadpool2 <Fiber> ::thread () {
    while (true)
    switch (WaitForSingleObject (this->semaphore, INFINITE)) {
        case WAIT_OBJECT_0:

            if (this->quit) {
                if (InterlockedDecrement (&this->threads) == 0) {
                    SetEvent (this->done);
                }
                return 0;

            } else {
                auto work = this->queue [InterlockedIncrement (&this->consumed) - 1];
                try {
                    (work.fiber->*(work.function)) ();
                } catch (...) {

                }
                if (InterlockedDecrement (&this->remains) == 0) {
                    SetEvent (this->done);
                }
            }
            break;

        default:
        case WAIT_FAILED:
        case WAIT_ABANDONED:
            return GetLastError ();
    }
}

#endif

