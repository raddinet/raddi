#ifndef RADDI_LOCK_H
#define RADDI_LOCK_H

#include <windows.h>

class lock {
    void * srw = nullptr;
    
    static void (WINAPI * pInit) (void **);
    static void (WINAPI * pFree) (void **);
    static void (WINAPI * pAcquireShared) (void **);
    static void (WINAPI * pAcquireExclusive) (void **);
    static void (WINAPI * pReleaseShared) (void **);
    static void (WINAPI * pReleaseExclusive) (void **);
public:
    static bool initialize () noexcept;
public:
    lock () noexcept { pInit (&this->srw); }
    ~lock () noexcept { pFree (&this->srw); }

    void acquire_shared () noexcept { pAcquireShared (&this->srw); }
    void release_shared () noexcept { pReleaseShared (&this->srw); }

    void acquire_exclusive () noexcept { pAcquireExclusive (&this->srw); }
    void release_exclusive () noexcept { pReleaseExclusive (&this->srw); }

private:
    lock (const lock &) = delete;
    lock & operator = (const lock &) = delete;
};

class immutability {
    lock & ref;
public:
    explicit immutability (lock & ref) noexcept : ref (ref) {
        this->ref.acquire_shared ();
    }
    ~immutability () noexcept {
        this->ref.release_shared ();
    }
};

class exclusive {
    lock & ref;
public:
    explicit exclusive (lock & ref) noexcept : ref (ref) {
        this->ref.acquire_exclusive ();
    }
    ~exclusive () noexcept {
        this->ref.release_exclusive ();
    }
};

#endif
