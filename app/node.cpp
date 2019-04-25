#include "node.h"

namespace {
    HANDLE iocp = NULL;
    HANDLE thread = NULL;

    template <typename C, long (C::*fn) ()>
    DWORD WINAPI forward (LPVOID lpVoid) noexcept {
        return (static_cast <C *> (lpVoid)->*fn) ();
    }
}

bool Node::initialize (const wchar_t * pid, DWORD message) {
    if (iocp == NULL) {
        iocp = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
    }
    if (thread == NULL) {
        thread = CreateThread (NULL, 0, forward <Node, &Node::worker>, this, 0, NULL);
    }

    if (iocp && thread) {
        this->parameter = pid;
        this->guiMessage = message;
        this->guiThreadId = GetCurrentThreadId ();
        this->report (raddi::log::level::note, 0x20, this->guiThreadId, this->parameter);
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

        return !raddi::older (heartbeat, now - 3 * 1'000'000uLL);
    } else
        return false;
}

long Node::worker () noexcept {
    bool         previous = false;
    BOOL         success;
    DWORD        n = 0;
    ULONG_PTR    key = 0;
    OVERLAPPED * overlapped = NULL;

    do {
        success = GetQueuedCompletionStatus (iocp, &n, &key, &overlapped, 250);
        if (overlapped) {
            try {
                static_cast <Overlapped *> (overlapped)->completion (success, n);

                // TODO: notify GUI thread (views) on change to tables
                // TODO: how to determine which table has changed (add db::table callback?)

            } catch (const std::bad_alloc & x) {
                this->report (raddi::log::level::error, 0x21, x.what ());
                PostThreadMessage (this->guiThreadId, this->guiMessage, 1, ERROR_NOT_ENOUGH_MEMORY);
            } catch (const std::exception & x) {
                this->report (raddi::log::level::error, 0x21, x.what ());
                PostThreadMessage (this->guiThreadId, this->guiMessage, 1, 0);
            } catch (...) {
                this->report (raddi::log::level::error, 0x20);
                PostThreadMessage (this->guiThreadId, this->guiMessage, 1, 0);
                Sleep (10);
            }
        } else
        if (!success && (GetLastError () == WAIT_TIMEOUT)) {
            n = 1; // continue loop
        }

        if (!this->connected ()) {
            this->disconnect ();
            if (previous) {
                previous = false;
                this->report (raddi::log::level::note, 0x22);
                PostThreadMessage (this->guiThreadId, this->guiMessage, 0, FALSE);
            }
            if (this->reconnect ()) {
                this->report (raddi::log::level::note, 0x21);
                PostThreadMessage (this->guiThreadId, this->guiMessage, 0, TRUE);
                previous = true;
            }
        }
        
    } while (overlapped || key || n);

    this->report (raddi::log::level::note, 0x2F);
    this->disconnect ();
    return 0;
}

bool Node::reconnect () {
    try {
        this->instance = new raddi::instance (parameter);
        if (this->instance->status == ERROR_SUCCESS) {
            this->database = new raddi::db (file::access::read, this->instance->get <std::wstring> (L"database"));

            return this->database->connected ();
        }
    } catch (...) {
    }
    return false;
}

bool Node::disconnect () {
    if (this->database || this->instance) {
        delete this->database;
        delete this->instance;

        this->database = nullptr;
        this->instance = nullptr;
        return true;
    } else
        return false;
}

// glue async operations of RADDI core to worker thread running here

bool Overlapped::await (HANDLE handle, void * key) noexcept {
    return CreateIoCompletionPort (handle, iocp, (ULONG_PTR) key, 0) == iocp;
}
bool Overlapped::enqueue () noexcept {
    return PostQueuedCompletionStatus (iocp, 0, 0, this);
}
