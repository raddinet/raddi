#include "resolver.h"
#include "../common/node.h"

extern DWORD gui;
extern Node connection;

namespace {
    template <typename C, long (C:: * fn) ()>
    DWORD WINAPI forward (LPVOID lpVoid) noexcept {
        return (static_cast <C *> (lpVoid)->*fn) ();
    }

    // u82ws
    //  - converts UTF-8 to UTF-16 (wchar_t) string
    //
    std::wstring u82ws (const uint8_t * data, std::size_t size) {
        std::wstring result;
        if (data && size) {
            if (auto n = MultiByteToWideChar (CP_UTF8, 0, ( LPCCH) data, ( int) size, NULL, 0)) {
                result.resize (n);
                MultiByteToWideChar (CP_UTF8, 0, ( LPCCH) data, ( int) size, &result [0], n);
            };
        }
        return result;
    }
}

bool Resolver::initialize (DWORD message) {
    if (this->finish == NULL) {
        this->finish = CreateEvent (NULL, FALSE, FALSE, NULL);
    }
    if (this->process == NULL) {
        this->process = CreateEvent (NULL, FALSE, FALSE, NULL);
    }
    if (this->thread == NULL) {
        this->thread = CreateThread (NULL, 0, forward <Resolver, &Resolver::worker>, this, CREATE_SUSPENDED, NULL);
        if (this->thread) {
            SetThreadPriorityBoost (this->thread, TRUE); // disable SetEvent boosting worker priority
        }
    }

    if (this->finish && this->process && this->thread) {
        this->message = message;
        this->report (raddi::log::level::note, 0x30);
        return true;
    } else {
        this->terminate ();
        return false;
    }
}

void Resolver::terminate () {
    if (this->finish && this->thread) {
        SetEvent (this->finish);
        ResumeThread (this->thread);
        WaitForSingleObject (this->thread, INFINITE);
    }
    if (this->thread) {
        CloseHandle (this->thread);
        this->thread = NULL;
    }
    for (auto & e : this->events) {
        CloseHandle (e);
        e = NULL;
    }
}

void Resolver::start () {
    ResumeThread (this->thread);
}
void Resolver::advance (enum Node::table, std::uint32_t shard, std::uint32_t limit) {

    SetEvent (this->process);
}

void Resolver::change_rules (HWND window, const Rules & ruleset) {
    exclusive guard (this->changes.lock);
    this->changes.rules [window] = ruleset;
    SetEvent (this->process);
}
void Resolver::clear (HWND window) {
    exclusive guard (this->changes.lock);
    this->changes.rules.erase (window);
    this->changes.clear.insert (window);
    SetEvent (this->process);
}


bool Resolver::add (HWND requester, const raddi::eid & entry) {
    exclusive guard (this->changes.lock);

    // check if already registered
    // first if entry exists in resolved items

    auto ii = this->items.find (entry);
    if (ii != this->items.end ()) {
        auto hWnd = GetAncestor (requester, GA_ROOT);
        auto id = GetDlgCtrlID (requester);

        // next if parent hWnd exists among our list

        auto n = this->windows.size ();
        for (auto i = 0u; i != n; ++i) {
            if (this->windows [i].hWnd == hWnd) {

                // then if the pair window/id exists among registrations for this entry

                for (const auto & registration : ii->second) {
                    if ((registration.window == i) && (registration.child == id)) {

                        return false;
                    }
                }
            }
        }
    }

    if (this->changes.add [entry].insert (requester).second) {
        SetEvent (this->process);
        return true;
    } else
        return false;
}

bool Resolver::get_original_title (const raddi::eid & entry, std::wstring * text) {
    auto ii = this->results.initial.find (entry);
    if (ii != this->results.initial.end ()) {

        *text = ii->second;
        return true;
    } else
        return false;
}

bool Resolver::get (const raddi::eid & entry, HWND requester, std::wstring * text) {
    auto hWnd = GetAncestor (requester, GA_ROOT);

    immutability guard (this->results.lock);

    auto n = this->windows.size ();
    for (auto i = 0u; i != n; ++i) {
        if (this->windows [i].hWnd == hWnd) {
            
            // requestor's parent is already registered, can be already resolved

            auto ir = this->results.resolved.find (entry);
            if (ir != this->results.resolved.end ()) {

                auto irw = ir->second.find (i);
                if (irw != ir->second.end ()) {

                    // gotcha
                    *text = irw->second;
                    this->report (raddi::log::level::note, 0x34, irw->second, entry, hWnd, i);
                    return true;
                }
            }

            break;
        }
    }

    // not registered or not yet resolved, in any case re-register for retrieval

    bool added = this->add (requester, entry);

    // do we have at least initial?

    if (this->get_original_title (entry, text)) {
        this->report (raddi::log::level::note, 0x35, *text, entry, hWnd, added);
        return true;
    } else
        return false;
}

void Resolver::remove (HWND requester) {
    exclusive guard (this->changes.lock);

    for (auto & adds : this->changes.add) {
        adds.second.erase (requester);
    }
    this->changes.remove.insert (requester);
}

long Resolver::worker () noexcept {
    char buffer [sizeof (raddi::entry) + raddi::entry::max_content_size];
    while (WaitForMultipleObjects (2, this->events, FALSE, INFINITE) == WAIT_OBJECT_0) { // exits on error, or finish event

        // reflect add/clear requests into 'items' set
        this->process_changes ();
        this->clean_abandoned ();

        if (!this->items.empty ()) {

            // continue with low priority after data moved
            SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);
            SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_BEGIN);

            std::set <Registration> recipients;
            try {
                std::uint32_t bottom = this->items.begin ()->first.timestamp;

                exclusive guard (connection.lock);
                if (connection.connected ()) {

                    // first get creation name of the identity/channel
                    //  - only of those not already resolved for initial title

                    for (auto & [eid, wnds] : this->changes.add) {
                        if (!this->results.initial.count (eid)) {

                            bool exists = false;
                            std::size_t n = 0;
                            std::size_t skip = 0;

                            if (eid.timestamp == eid.identity.timestamp) {
                                // identity
                                exists = connection.database->identities->get (eid.identity, raddi::db::read::content, buffer, &n);
                                skip = sizeof (raddi::identity) - sizeof (raddi::entry);
                            } else {
                                // channel
                                exists = connection.database->channels->get (eid, raddi::db::read::content, buffer, &n);
                                skip = sizeof (raddi::channel) - sizeof (raddi::entry);
                            }

                            if (exists) {
                                const auto entry = reinterpret_cast <raddi::entry *> (buffer);
                                std::size_t proof_size = 0;
                                if (entry->proof (n, &proof_size)) {

                                    auto text = entry->content () + skip;
                                    if (auto length = raddi::content::is_plain_line (text, n - proof_size - skip)) {

                                        // will be reporting changes to all who registered for this
                                        for (auto hWnd : wnds) {
                                            recipients.insert (Registration (this, hWnd));
                                        }

                                        // finally write the title, only if not empty
                                        exclusive guard2 (this->results.lock);
                                        this->results.initial [eid] = u82ws (text, length);
                                    }
                                }
                            } else {
                                this->items.erase (eid);
                            }
                        }
                    }
                    this->changes.add.clear ();

                    // now go through all 'threads' for title change entries

                    connection.database->threads->select (bottom, raddi::now (),
                                                          [this](const auto & row, const auto &) {
                                                              return this->items.count (row.parent);
                                                          },
                                                          [this](const auto & row, const auto & detail) { return true; },
                                                              [this, &recipients](const auto & row, const auto & detail, std::uint8_t * data) {

                                                              // decode content
                                                              auto x = raddi::content::analyze (data, row.data.length);

                                                              // TODO: is this entry that affects title?
                                                              // BS byte means revert?

                                                              for (auto registration : this->items [row.parent]) {

                                                                  // does this change (author) apply according to rules?

                                                                  // TODO: identities can be renamed only by author
                                                                  //   - but can be banned by moderator we follow

                                                                  // if (this->windows [registration.window].rules) {

                                                                      // update set of recipients we need to notify of this change
                                                                      //recipients.insert (registration);
                                                                  // }
                                                              }

                                                              // TODO: keep checking for 'finish' event sometimes?
                                                          });
                }
            } catch (const std::bad_alloc &) {

            }

            // move fresh to resolved
            // this->update_resolved (fresh);

            // prevent locking in update
            SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_NORMAL);
            SetThreadPriority (GetCurrentThread (), THREAD_MODE_BACKGROUND_END);

            // notify all recipients

            for (auto recipient : recipients) {
                if (this->windows [recipient.window].hWnd) {
                    SendMessage (this->windows [recipient.window].hWnd, this->message, recipient.child, 0);
                }
            }
        }
    }

    this->report (raddi::log::level::note, 0x3F);
    return 0;
}

void Resolver::process_changes () {
    exclusive guard (this->changes.lock);

    // replace rules
    for (auto & [hWnd, rules] : this->changes.rules) {
        this->window (hWnd).rules = rules;
    }
    
    // remove items for windows that are to be destroyed
    auto removed = 0u;
    for (auto hWnd : this->changes.clear) {
        auto index = this->window_index (hWnd);
        
        for (auto & item : this->items) {
            auto i = item.second.begin ();
            auto e = item.second.end ();

            while (i != e) {
                if (i->window == index) {
                    i = item.second.erase (i);
                    ++removed;
                } else
                    ++i;
            }
        }

        // and clear the registration for the window
        this->windows [index].hWnd = NULL;
    }
    
    // remove pending to be erased
    for (auto control : this->changes.remove) {
        Registration r (this, control);

        for (auto item : this->items) {
            item.second.erase (r);
        }
    }

    // merge in new requests
    auto added = 0u;
    for (auto & [entry, controls] : this->changes.add) {
        for (auto control : controls) {
            if (this->items [entry].insert (Registration (this, control)).second) {
                ++added;
            }
        }
    }

    if (added || removed
            || this->changes.rules.size () || this->changes.clear.size ()
            || this->changes.remove.size () || this->changes.add.size ()) {

        this->report (raddi::log::level::note, 0x31,
                      this->changes.rules.size (), this->changes.clear.size (), removed,
                      this->changes.remove.size (), this->changes.add.size (), added);
    }

    // clear processed requests
    this->changes.rules.clear ();
    this->changes.clear.clear ();
    this->changes.remove.clear ();
}

std::size_t Resolver::window_index (HWND hWnd) {
    auto n = this->windows.size ();
    for (auto i = 0u; i != n; ++i) {
        if (this->windows [i].hWnd == hWnd) {
            return i;
        }
    }
    for (auto i = 0u; i != n; ++i) {
        if (this->windows [i].hWnd == NULL) {
            this->windows [i].hWnd = hWnd;
            this->report (raddi::log::level::note, 0x33, hWnd, i);
            return i;
        }
    }
    this->windows.resize (n + 1);
    this->windows [n].hWnd = hWnd;
    this->report (raddi::log::level::note, 0x33, hWnd, n);
    return n;
}

Resolver::Window & Resolver::window (HWND hWnd) {
    return this->windows [this->window_index (hWnd)];
}

void Resolver::clean_abandoned () {
    auto n = 0u;
    auto i = this->items.begin ();
    auto e = this->items.end ();

    while (i != e) {
        if (i->second.empty ()) {
            auto eid = i->first;

            i = this->items.erase (i);
            ++n;

            exclusive guard (this->results.lock);
            this->results.initial.erase (eid);
            this->results.resolved.erase (eid);
        } else
            ++i;
    }
    if (n) {
        this->report (raddi::log::level::note, 0x32, n);
    }
}
