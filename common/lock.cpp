#include "lock.h"

namespace {
    void WINAPI defaultCsInit (void ** object) {
        auto cs = new CRITICAL_SECTION;
        InitializeCriticalSectionAndSpinCount (cs, 32);
        *object = cs;
    }
    void WINAPI defaultInit (void ** object) {
        if (!lock::initialize ()) {
            defaultCsInit (object);
        }
    }
    void WINAPI defaultFree (void ** object) {
        auto cs = static_cast <CRITICAL_SECTION *> (*object);
        DeleteCriticalSection (cs);
        delete cs;
    }
    bool WINAPI defaultTry (void ** object) {
        return TryEnterCriticalSection (static_cast <CRITICAL_SECTION *> (*object));
    }
    bool WINAPI failingTry (void ** object) {
        return false;
    }
    void WINAPI defaultAcquire (void ** object) {
        EnterCriticalSection (static_cast <CRITICAL_SECTION *> (*object));
    }
    void WINAPI defaultRelease (void ** object) {
        LeaveCriticalSection (static_cast <CRITICAL_SECTION *> (*object));
    }
    void WINAPI defaultNoOp (void ** object) {}

    template <typename P>
    bool Load (HMODULE h, P & pointer, const char * name) {
        if (P p = reinterpret_cast <P> (GetProcAddress (h, name))) {
            pointer = p;
            return true;
        } else
            return false;
    }
}

void (WINAPI * lock::pInit) (void **) = defaultInit;
void (WINAPI * lock::pFree) (void **) = defaultFree;
void (WINAPI * lock::pAcquireShared) (void **) = defaultAcquire;
void (WINAPI * lock::pAcquireExclusive) (void **) = defaultAcquire;
bool (WINAPI * lock::pTryAcquireShared) (void **) = defaultTry;
bool (WINAPI * lock::pTryAcquireExclusive) (void **) = defaultTry;
void (WINAPI * lock::pReleaseShared) (void **) = defaultRelease;
void (WINAPI * lock::pReleaseExclusive) (void **) = defaultRelease;

bool lock::initialize () noexcept {
    if (auto h = GetModuleHandle (L"KERNEL32")) {
        if (Load (h, pAcquireShared, "AcquireSRWLockShared")) {
            Load (h, pReleaseShared, "ReleaseSRWLockShared");
            Load (h, pAcquireExclusive, "AcquireSRWLockExclusive");
            Load (h, pReleaseExclusive, "ReleaseSRWLockExclusive");

            // Vista does not have TryAcquire for SRW locks, but we cannot mix CSs and SRWs
            if (!Load (h, pTryAcquireShared, "TryAcquireSRWLockShared") || !Load (h, pTryAcquireExclusive, "TryAcquireSRWLockExclusive")) {
                pTryAcquireShared = failingTry;
                pTryAcquireExclusive = failingTry;
            }

            pInit = defaultNoOp;
            pFree = defaultNoOp;
            return true;
        }
    }
    pInit = defaultCsInit;
    return false;
}
