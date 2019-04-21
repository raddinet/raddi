#include "node.h"

namespace {
    HANDLE iocp = NULL;
    HANDLE thread = NULL;

    template <typename C, long (C::*fn) ()>
    DWORD WINAPI forward (LPVOID lpVoid) {
        return (static_cast <C *> (lpVoid)->*fn) ();
    }
}

bool Node::initialize (const wchar_t * pid) {
    iocp = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    thread = CreateThread (NULL, 0, forward <Node, &Node::worker>, this, 0, NULL);

    if (iocp && thread) {
        this->parameter = pid;
        return true;
    } else
        return false;
}

void Node::terminate () {
    if (iocp && thread) {
        PostQueuedCompletionStatus (iocp, 0, 0, NULL);
        WaitForSingleObject (thread, INFINITE);
    }
    if (thread) {
        CloseHandle (thread);
    }
    if (iocp) {
        CloseHandle (iocp);
    }
    iocp = NULL;
}

bool Node::connected () const {
    if (this->instance && this->database && this->database->connected ()) {

        auto heartbeat = this->instance->get <unsigned long long> (L"heartbeat");
        auto now = raddi::microtimestamp ();

        return (now - heartbeat) < (3 * 1'000'000uLL);
    } else
        return false;
}

long Node::worker () {
    BOOL         success;
    DWORD        n = 0;
    ULONG_PTR    key = 0;
    OVERLAPPED * overlapped = NULL;

    do {
        success = GetQueuedCompletionStatus (iocp, &n, &key, &overlapped, 1000);
        if (overlapped) {
            try {
                static_cast <Overlapped *> (overlapped)->completion (success, n);

            } catch (const std::exception & x) {
                //raddi::log::error (6, i, id, x.what ());
            } catch (...) {
                //raddi::log::stop (7, i, id);
                PostQueuedCompletionStatus (iocp, 0, 0, NULL);
            }
        }

        if (!success && (GetLastError () == WAIT_TIMEOUT)) {

            // log

            if (!this->connected ()) {
                delete this->database;
                delete this->instance;

                this->database = nullptr;
                this->instance = nullptr;

                try {
                    this->instance = new raddi::instance (this->parameter);
                    this->database = new raddi::db (file::access::read, this->instance->get <std::wstring> (L"database"));

                } catch (...) {

                }
                // reconnect

                // TODO: notify GUI thread (views) on change to tables
                // TODO: process monitor changes (and other awaits) on this thread
            }
        }
    } while (overlapped || key || n);

    // log

    return 0;
}

// glue async operations of RADDI library to worker thread running here

bool Overlapped::await (HANDLE handle, void * key) noexcept {
    return CreateIoCompletionPort (handle, iocp, (ULONG_PTR) key, 0) == iocp;
}
bool Overlapped::enqueue () noexcept {
    return PostQueuedCompletionStatus (iocp, 0, 0, this);
}
