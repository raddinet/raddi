#include "resolver.h"
#include "node.h"

extern DWORD gui;
extern Node connection;

namespace {
    template <typename C, long (C:: * fn) ()>
    DWORD WINAPI forward (LPVOID lpVoid) noexcept {
        return (static_cast <C *> (lpVoid)->*fn) ();
    }
}

bool Resolver::initialize (DWORD message) {
    if (this->finish == NULL) {
        this->finish = CreateEvent (NULL, FALSE, FALSE, NULL);
    }
    if (this->update == NULL) {
        this->update = CreateEvent (NULL, FALSE, FALSE, NULL);
    }
    if (this->thread == NULL) {
        this->thread = CreateThread (NULL, 0, forward <Resolver, &Resolver::worker>, this, CREATE_SUSPENDED, NULL);
    }

    if (this->finish && this->update && this->thread) {
        this->message = message;
        // this->report (raddi::log::level::note, 0x20, gui);
        return true;
    } else
        return false;
}

void Resolver::terminate () {
    if (this->finish && this->thread) {
        SetEvent (this->finish);
        ResumeThread (this->thread);
        WaitForSingleObject (this->thread, INFINITE);
    }
    if (this->thread) {
        CloseHandle (this->thread);
    }
    if (this->finish) {
        CloseHandle (this->finish);
    }
    if (this->update) {
        CloseHandle (this->update);
    }
    this->thread = NULL;
    this->finish = NULL;
    this->update = NULL;
}

void Resolver::start () {
    ResumeThread (this->thread);
}

void Resolver::evaluate () {
    SetEvent (this->update);
    // exclusive guard (this->lock);
    // TODO: consider all new entries in 'threads' table
}

void Resolver::add (const raddi::eid & entry) {
    exclusive guard (this->lock);

    // this->
}

long Resolver::worker () noexcept {
    SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);
    SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

    // exits on error, or finish event

    HANDLE events [] = { this->finish, this->update };
    while (WaitForMultipleObjects (2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        // something changed
        // TODO: if new objects arrived, resolve them
        // TODO: resolve already stored if db changed
        // TODO: keep checking for 'finish' event


        // logic:
        // if new entry is direct descendant of requested (user, channel) then it can be rename/update, then mark as dirty, and set event

        // TODO: apply only changes from authors (if not unsubbed) and moderators that we are subscribed to


        // update 'top'

        // TODO: after posting send message and wait for reply (somehow) so that we can free memory
    }

    try {

    } catch (const std::bad_alloc & x) {
        // this->report (raddi::log::level::error, 0x21, x.what ());
        // PostThreadMessage (gui, this->message, 1, ERROR_NOT_ENOUGH_MEMORY);
    } catch (const std::exception & x) {
        // this->report (raddi::log::level::error, 0x21, x.what ());
        // PostThreadMessage (gui, this->message, 1, 0);
    } catch (...) {
        // this->report (raddi::log::level::error, 0x20);
        // PostThreadMessage (gui, this->message, 1, 0);
        Sleep (10);
    }
    // this->report (raddi::log::level::note, 0x2F);


    return 0;
}
