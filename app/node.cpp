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

bool Node::connected () const  noexcept {
    if (this->instance && this->database && this->database->connected ()) {

        auto heartbeat = this->instance->get <unsigned long long> (L"heartbeat");
        auto now = raddi::microtimestamp ();

        return !raddi::older (heartbeat, now - 3 * 1'000'000uLL);
    } else
        return false;
}

long Node::worker () noexcept {
    bool         previous = false;
    bool         timeout = false;
    DWORD        n = 0;
    ULONG_PTR    key = 0;
    OVERLAPPED * overlapped = NULL;

    do {
        auto success = GetQueuedCompletionStatus (iocp, &n, &key, &overlapped, 250);
        if (overlapped) {
            try {
                static_cast <Overlapped *> (overlapped)->completion (success, n);
                // TODO: notify GUI thread... how to determine which table has changed (add db::table callback?)

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
            timeout = false;
        } else {
            timeout = (!success && (GetLastError () == WAIT_TIMEOUT));
        }

        if (timeout) {
            this->lock.acquire_exclusive ();
        } else {
            if (!this->lock.try_acquire_exclusive ())
                continue;
        }

        int report = 0;
        if (!this->connected ()) {
            this->disconnect ();
            if (previous) {
                previous = false;
                report = 1;
            }
            if (this->reconnect ()) {
                report = 2;
                previous = true;
            }
        }

        this->lock.release_exclusive ();

        // notify GUI

        switch (report) {
            case 1:
                PostThreadMessage (this->guiThreadId, this->guiMessage, 0, FALSE);
                this->report (raddi::log::level::note, 0x22);
                break;
            case 2:
                PostThreadMessage (this->guiThreadId, this->guiMessage, 0, TRUE);
                this->report (raddi::log::level::note, 0x21);
                break;
        }
        
    } while (overlapped || key || n || timeout);

    this->report (raddi::log::level::note, 0x2F);
    this->disconnect ();
    return 0;
}

template <unsigned int X>
void Node::db_table_change_notify (void * self_) {
    auto self = reinterpret_cast <Node *> (self_);

    PostThreadMessage (self->guiThreadId, self->guiMessage, 2, X);
    this->report (raddi::log::level::note, 0x23, X);
}

bool Node::reconnect ()  noexcept {
    try {
        this->instance = new raddi::instance (parameter);
        if (this->instance->status == ERROR_SUCCESS) {
            this->database = new raddi::db (file::access::read, this->instance->get <std::wstring> (L"database"));

            this->database->data->notification_callback_context = this;
            this->database->threads->notification_callback_context = this;
            this->database->channels->notification_callback_context = this;
            this->database->identities->notification_callback_context = this;

            this->database->data->reader_change_notification_callback = &Node::db_table_change_notify <0u>;
            this->database->threads->reader_change_notification_callback = &Node::db_table_change_notify <1u>;
            this->database->channels->reader_change_notification_callback = &Node::db_table_change_notify <2u>;
            this->database->identities->reader_change_notification_callback = &Node::db_table_change_notify <3u>;

            return this->database->connected ();
        }
    } catch (...) {
    }
    return false;
}

bool Node::disconnect () noexcept {
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
