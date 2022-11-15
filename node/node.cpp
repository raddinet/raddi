#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <winhttp.h>
#include <mstcpip.h>
#include <mswsock.h>
#include <shlobj.h>
#include <powrprof.h>

#include <VersionHelpers.h>

#include <stdexcept>
#include <cstdarg>
#include <list>

#include <sodium.h>
#include <lzma.h>

#include "../common/log.h"
#include "../common/file.h"
#include "../common/lock.h"
#include "../common/options.h"
#include "../common/platform.h"

#include "server.h"
#include "source.h"
#include "timers.h"
#include "dns.h"
#include "download.h"
#include "localhosts.h"

#include "../core/raddi_defaults.h"
#include "../core/raddi_connection.h"
#include "../core/raddi_coordinator.h"
#include "../core/raddi_command.h"
#include "../core/raddi_request.h"
#include "../core/raddi_noticed.h"
#include "../core/raddi_content.h"
#include "../core/raddi_consensus.h"
#include "../core/raddi_instance.h"

#pragma warning (disable:28112) // interlocked warnings
#pragma warning (disable:26812) // unscoped enum warning
#pragma warning (disable:6262) // stack usage warning

namespace {
    SERVICE_STATUS_HANDLE handle = NULL;
    SERVICE_STATUS status = {
        SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING,
        SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN,
        0, 0, 0, 1000
    };

    BOOL WINAPI console (DWORD);
    void WINAPI service (DWORD, LPWSTR *);
    void WINAPI http (HINTERNET, DWORD_PTR, DWORD, LPVOID, DWORD);
    
    DWORD WINAPI handler (DWORD code, DWORD event, LPVOID, LPVOID);
    DWORD WINAPI worker (LPVOID);

    HANDLE iocp = NULL;
    HANDLE optimize = NULL;
    HANDLE workclock = NULL;
    HANDLE terminating = NULL;
    HANDLE disconnected = NULL;

    const auto version = GetCurrentProcessVersionInfo ();
    SERVICE_TABLE_ENTRY services [] = {
        { const_cast <LPWSTR> (raddi::defaults::service_name), service },
        { NULL, NULL }
    };
    
    void terminate ();
    bool embrace (raddi::connection * source, const raddi::entry * entry, std::size_t size, std::size_t nesting = 0);
    bool assess_proof_requirements (const raddi::entry * entry, const raddi::proof * proof, bool & disconnect);

    std::size_t          workers = 0;
    raddi::db *          database = nullptr;
    raddi::coordinator * coordinator = nullptr;
    LocalHosts *         localhosts = nullptr; // TODO: consider making member of 'coordinator'
    Download *           downloader = nullptr;
    Dns *                dns = nullptr;
}

int wmain (int argc, wchar_t ** argw) {
    SetErrorMode (0x8007);
    SetDllDirectoryW (L"");
    RegDisablePredefinedCache ();
    InitPlatformAPI ();

    ULONG heapmode = 2;
    HeapSetInformation (GetProcessHeap (), HeapCompatibilityInformation, &heapmode, sizeof heapmode);
    HeapSetInformation (GetProcessHeap (), HeapEnableTerminationOnCorruption, NULL, 0);

    if (WSAInitialize ()
        && (iocp = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0))
        && (optimize = CreateEvent (NULL, FALSE, FALSE, NULL))
        && (workclock = CreateSemaphore (NULL, 0, 0x7FFFFFFF, NULL))
        && (terminating = CreateEvent (NULL, FALSE, FALSE, NULL))
        && (disconnected = CreateEvent (NULL, FALSE, FALSE, NULL))
        && (sodium_init () != -1)) {

        if (IsWindowsVistaOrGreater ()) {
            status.dwControlsAccepted |= SERVICE_ACCEPT_PRESHUTDOWN;
        }
        if (IsWindows7OrGreater ()) {
            status.dwControlsAccepted |= SERVICE_ACCEPT_TIMECHANGE;
        }
        if (IsWindowsBuildOrGreater (10, 0, 15063)) {
            status.dwControlsAccepted |= SERVICE_ACCEPT_LOWRESOURCES | SERVICE_ACCEPT_SYSTEMLOWRESOURCES;
        }

        if (auto parameter = option (argc, argw, L"name")) {
            services [0].lpServiceName = const_cast <LPWSTR> (parameter);
        }

        if (!StartServiceCtrlDispatcher (services)) {

            status.dwWin32ExitCode = GetLastError ();
            if (status.dwWin32ExitCode == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                SetConsoleCtrlHandler (console, true);

                argw [0] = services [0].lpServiceName;
                service (argc, argw);
            }
        }

        WSACleanup ();

        if (status.dwWin32ExitCode != NO_ERROR) {
            raddi::log::error (0xFF, raddi::log::api_error (status.dwWin32ExitCode));
        } else {
            raddi::log::event (0xFF);
        }
        return status.dwWin32ExitCode;
    } else
        return GetLastError ();
}

// glue member functions:

bool Overlapped::await (HANDLE handle, void * key) noexcept {
    return CreateIoCompletionPort (handle, iocp, (ULONG_PTR) key, 0) == iocp;
}
bool Overlapped::enqueue () noexcept {
    return PostQueuedCompletionStatus (iocp, 0, 0, this);
}

bool Listener::connected (sockaddr * local, sockaddr * remote) {
    this->report (raddi::log::level::event, 1, remote, local, (SOCKET) this->prepared);
    return ::coordinator->incomming (std::move (this->prepared), remote);
}

bool raddi::coordinator::is_local (const raddi::address & a) const {
    return ::localhosts->contains (a);
}

bool raddi::discovery::is_local (const raddi::address & a) const {
    return ::localhosts->contains (a);
}
void raddi::discovery::discovered (const address & address) {
    if (!::coordinator->find (address)) {
        this->report (raddi::log::level::event, 5, address);
        ::coordinator->add (announced_nodes, address);
    }
}
void raddi::discovery::out_of_memory () {
    SetEvent (::optimize);
}
void raddi::connection::out_of_memory () {
    SetEvent (::optimize);
}

void raddi::connection::discord () {
    ::coordinator->disagreed (this);
}
void raddi::connection::disconnected () {
    if (this->secured) {
        this->report (raddi::log::level::event, 2, this->peer);
    } else {
        this->report (raddi::log::level::event, 3, this->peer);
        ::coordinator->unavailable (this);
    }

    this->Socket::disconnect ();
    this->secured = false;
    this->retired = true; // 'this' can cease to exist at any time after this line

    SetEvent (::disconnected);
}
bool raddi::connection::head (raddi::protocol::initial * peer) {
    if (::localhosts->contains (this->peer) || ::coordinator->reflecting (&peer->keys)) {
        this->report (raddi::log::level::note, 3);
        return false;
    }
    if (::coordinator->reciprocal (this)) {
        this->report (raddi::log::level::note, 5);
        return false;
    }

    raddi::protocol::accept_fail_reason failure;
    if (auto ee = this->proposal->accept (peer, &failure, (this->peer.port == 0))) {

        // replace proposal with encryption
        delete this->proposal;
        this->encryption = ee;

        this->report (raddi::log::level::event, 4, ee->reveal ());

        // this may also call 'send' thus we need to have the 'encryption' above already set
        ::coordinator->established (this);
        return true;
    } else {
        switch (failure) {
            case raddi::protocol::accept_fail_reason::checksum:
                this->report (raddi::log::level::error, 17);
                break;
            case raddi::protocol::accept_fail_reason::flags:
                this->report (raddi::log::level::error, 18, peer->flags.hard.decode ());
                break;
            case raddi::protocol::accept_fail_reason::time:
                this->report (raddi::log::level::error, 19, (std::int64_t) (peer->timestamp - raddi::microtimestamp ())); // BUG: timestamp is XORed
                break;
            case raddi::protocol::accept_fail_reason::aes:
                this->report (raddi::log::level::error, 20);
                break;

            default:
                this->report (raddi::log::level::note, 10, peer->flags.soft.decode ());
        }
        return false;
    }
}
bool raddi::connection::message (const unsigned char * data, std::size_t size) {
    if (size >= sizeof (raddi::entry) + raddi::proof::min_size) {
        if (raddi::entry::validate (data, size)) {

            const auto entry = reinterpret_cast <const raddi::entry *> (data);
            const auto proof = entry->proof (size);

            bool disconnect;
            if (assess_proof_requirements (entry, proof, disconnect)) {
                return embrace (this, entry, size);
            } else
                return disconnect;

        } else
            return false;
    } else
        return ::coordinator->process (data, size, this);
};

bool Source::entry (const raddi::entry * data, std::size_t size) {
    try {
        if (raddi::entry::validate (data, size)) {
            return embrace (nullptr, data, size);
        } else
            return false;
    } catch (...) {
        // TODO: report
        return false;
    }
}

namespace {
    // TODO: raddi::node::settings
    struct {

        // proof_complexity_requirements_adjustment
        //  - use +1 to require higher level of complexity and drop all other entries
        //  - use -1 to allow lower difficulty identities/channels
        //
        int proof_complexity_requirements_adjustment = 0;

        // track_all_channels
        //  - store top level channel entries to database ('threads' table)
        //    so that GUI apps can present meaningful info to the user
        //  - disabled for 'leaf' nodes
        //
        bool track_all_channels = true;

    } settings;
}

bool Source::command (const raddi::command * cmd, std::size_t size_) {
    auto size = size_ - sizeof (raddi::command);

    raddi::log::event (raddi::component::main, 5, cmd->type, size);

    // TODO: report on operation result/failure
    try {
        switch (cmd->type) {

            case raddi::command::type::test:
                break;

            case raddi::command::type::set_log_level:
            case raddi::command::type::set_display_level:
                // TODO: settings: allow log level changing, minimal/maximal allowed log level

                if (size < sizeof (raddi::log::level)) {
                    raddi::log::data (raddi::component::main, 8, cmd->type, sizeof (raddi::log::level));
                    return false;

                } else {
                    auto new_level = *reinterpret_cast <const raddi::log::level *> (cmd->content ());
                    switch (cmd->type) {
                        case raddi::command::type::set_log_level:
                            raddi::log::settings::level = new_level;
                            break;
                        case raddi::command::type::set_display_level:
                            raddi::log::settings::display = new_level;
                            break;
                    }
                }
                break;

            case raddi::command::type::optimize:
                SetEvent (optimize);
                break;
            case raddi::command::type::log_conn_status: {
                auto prev_log = raddi::log::settings::level;
                auto prev_display = raddi::log::settings::display;
                raddi::log::settings::display = raddi::log::level::note;
                raddi::log::settings::level = raddi::log::level::note;
                coordinator->status ();
                raddi::log::settings::level = prev_log;
                raddi::log::settings::display = prev_display;
            } break;

            // peers

            case raddi::command::type::add_peer:
            case raddi::command::type::rem_peer:
            case raddi::command::type::ban_peer:
            case raddi::command::type::unban_peer:
            case raddi::command::type::connect_peer:
            case raddi::command::type::add_core_peer:
                if (size < sizeof (raddi::address)) {
                    raddi::log::data (raddi::component::main, 8, cmd->type, sizeof (raddi::address));
                    return false;

                } else {
                    const auto & address = *reinterpret_cast <const raddi::address *> (cmd->content ());
                    if (address.valid (raddi::address::validation::allow_null_port)) {

                        switch (cmd->type) {
                            case raddi::command::type::add_peer:
                                if (address.port) {
                                    coordinator->add (raddi::level::announced_nodes, address);
                                }
                                break;

                            case raddi::command::type::rem_peer: // TODO: option to allow/disallow peer removal
                                database->peers [raddi::level::announced_nodes]->erase (address);
                                database->peers [raddi::level::validated_nodes]->erase (address);
                                database->peers [raddi::level::established_nodes]->erase (address);
                                database->peers [raddi::level::core_nodes]->erase (address);
                                break;

                            case raddi::command::type::ban_peer: // TODO: option to allow/disallow banning
                                coordinator->ban (address, 365); // TODO: 365 -> parameter?
                                break;
                            case raddi::command::type::unban_peer: // TODO: option to allow/disallow unbanning
                                coordinator->ban (address, 0);
                                break;

                            case raddi::command::type::connect_peer:
                                if (address.port) {
                                    coordinator->add (raddi::level::announced_nodes, address);
                                    coordinator->connect (address);
                                }
                                break;

                            case raddi::command::type::add_core_peer:// TODO: option to allow/disallow adding core peers
                                if (address.port) {
                                    coordinator->add (raddi::level::core_nodes, address);
                                }
                                break;
                        }
                    }
                }
                break;

            // data

            case raddi::command::type::download:
                if (size < sizeof (raddi::request::download)) {
                    raddi::log::data (raddi::component::main, 8, cmd->type, sizeof (raddi::request::download));
                    return false;

                } else {
                    coordinator->broadcast (raddi::request::type::download,
                                            cmd->content (), sizeof (raddi::request::download));
                }
                break;

            case raddi::command::type::erase:
            case raddi::command::type::erase_thorough:
                if (size < sizeof (raddi::eid)) {
                    raddi::log::data (raddi::component::main, 8, cmd->type, sizeof (raddi::eid));
                    return false;

                } else {
                    const auto & entry = *reinterpret_cast <const raddi::eid *> (cmd->content ());

                    // TODO: if (option) erase is allowed...

                    if (database->erase (entry, (cmd->type == raddi::command::type::erase_thorough) ? true : false)) {
                        raddi::log::event (raddi::component::main, 6, entry);
                    } else {
                        raddi::log::error (raddi::component::main, 9, entry);
                    }
                }
                break;

            // subscriptions

            case raddi::command::type::subscribe:
            case raddi::command::type::unsubscribe:
            case raddi::command::type::blacklist:
            case raddi::command::type::unblacklist:
            case raddi::command::type::retain:
            case raddi::command::type::unretain:
                if (size < sizeof (raddi::command::subscription)) {
                    raddi::log::data (raddi::component::main, 8, cmd->type, sizeof (raddi::command::subscription));
                    return false;

                } else {
                    const auto & subscription = *reinterpret_cast <const raddi::command::subscription *> (cmd->content ());

                    switch (cmd->type) {
                        case raddi::command::type::subscribe:
                            coordinator->subscribe (subscription.application, subscription.channel);
                            break;
                        case raddi::command::type::unsubscribe:
                            coordinator->unsubscribe (subscription.application, subscription.channel);
                            break;
                        case raddi::command::type::blacklist:
                            coordinator->blacklist.subscribe (subscription.application, subscription.channel);
                            break;
                        case raddi::command::type::unblacklist:
                            coordinator->blacklist.unsubscribe (subscription.application, subscription.channel);
                            break;
                        case raddi::command::type::retain:
                            coordinator->retained.subscribe (subscription.application, subscription.channel);
                            break;
                        case raddi::command::type::unretain:
                            coordinator->retained.unsubscribe (subscription.application, subscription.channel);
                            break;
                    }
                }
                break;

            default:
                raddi::log::data (raddi::component::main, 8, (unsigned int) cmd->type);
                return false;
        }
        return true;

    } catch (const std::exception & x) {
        raddi::log::error (raddi::component::main, 8, cmd->type, x.what ());
        return false;
    }
}

namespace {

    // assess_proof_requirements
    //  - checks if proof parameters match minimum required parameters for that entry type,
    //    optionally adjusted by user through a command-line parameter
    //
    bool assess_proof_requirements (const raddi::entry * entry, const raddi::proof * proof, bool & disconnect) {

        // validate complexity requirements for proof-of-work
        //  - applies adjustment

        if ((proof->complexity + proof->complexity_bias)
                < (entry->default_requirements ().complexity + settings.proof_complexity_requirements_adjustment)) {

            coordinator->refused.insert (entry->id);
            raddi::log::data (raddi::component::database, 0x17, entry->id, proof->complexity + proof->complexity_bias,
                              entry->default_requirements ().complexity + settings.proof_complexity_requirements_adjustment,
                              entry->default_requirements ().complexity, settings.proof_complexity_requirements_adjustment);

            // if adjusted manually, keep the connection, otherwise disconnect peer
            //  - at some point later we should revise necessity for this

            disconnect = (settings.proof_complexity_requirements_adjustment == 0);
            return false;
        } else
            return true;
    }

    // assess_extra_size_requirements
    //  - checks if entry content size is within consensus-mandated bounds
    //
    bool assess_extra_size_requirements (const raddi::entry * entry, std::size_t content_size, raddi::db::assessed_level level) {
        
        std::size_t max_content_size = raddi::entry::max_content_size;
        switch (level) {

            case raddi::db::assessed_level::thread:
                if (raddi::content::is_plain_line (entry->content (), content_size)) {
                    max_content_size = raddi::consensus::max_thread_name_size;
                } else {
                    max_content_size = raddi::consensus::max_channel_control_size;
                }
                break;

            default:
                return true;
        }

        if (content_size > max_content_size) {
            coordinator->refused.insert (entry->id);
            raddi::log::data (raddi::component::database, 0x1C, entry->id, content_size, max_content_size);
            return false;
        } else
            return true;
    }

    // assess_extra_proof_requirements
    //  - checks if proof parameters match minimum required parameters for that entry assessment level (thread, top level comment, nested, etc.)
    //
    bool assess_extra_proof_requirements (const raddi::entry * entry, const raddi::proof * proof, raddi::db::assessed_level level, bool & disconnect) {

        unsigned int requirement = 0;
        switch (level) {

            case raddi::db::assessed_level::thread:
                requirement = raddi::consensus::min_thread_pow_complexity;
                break;

            // not explicitly checked entries are allowed throught; already checked in 'assess_proof_requirements'

            default:
                return true;
        }

        if ((proof->complexity + proof->complexity_bias)
                < (requirement + settings.proof_complexity_requirements_adjustment)) {

            coordinator->refused.insert (entry->id);
            raddi::log::data (raddi::component::database, 0x17, entry->id, proof->complexity + proof->complexity_bias,
                              requirement + settings.proof_complexity_requirements_adjustment,
                              requirement, settings.proof_complexity_requirements_adjustment);

            // see 'assess_proof_requirements'
            disconnect = (settings.proof_complexity_requirements_adjustment == 0);
            return false;
        } else
            return true;
    }

    // TODO: move to 'raddi::node::insert' where 'node' will contain database, coordinator, glue functions and options loading
    //  - and only Win32 stuff will remain in node.cpp

    bool embrace (raddi::connection * source, const raddi::entry * entry, std::size_t size, std::size_t nesting) {
        const bool broadcast = (nesting == 0); // don't broadcast if called as part of detached reordering nesting, already have
        const bool old = raddi::older (entry->id.timestamp, raddi::now () - raddi::consensus::max_entry_age_allowed);
        bool inserted = false;

        raddi::db::root top;
        raddi::db::assessed_level level;
        switch (database->assess (entry, size, &top, &level)) {

            case raddi::db::rejected:
                if (source != nullptr) {
                    coordinator->detached.reject (entry->id);

                    if (++source->rejected > coordinator->settings.max_allowed_rejected_entries)
                        return false;
                }
                break;

            case raddi::db::detached:

                // we don't have parent
                try {
                    
                    // ensure partial threads from channel download doesn't fill 'detached'
                    //  - also old data downloads must come back sorted

                    if (!old) {
                        if (coordinator->refused.count (entry->parent)) {
                            // the entry->parent was refused then also refuse entry->id
                            coordinator->refused.insert (entry->id);
                            coordinator->detached.reject (entry->id);

                        } else {
                            // put to temporary cache for reordering
                            coordinator->detached.insert (entry->parent, entry, size);
                            raddi::log::note (raddi::component::database, 7, entry->id, entry->parent,
                                              coordinator->detached.size (), coordinator->detached.highwater);

                            // redistribute to other connections even if detached, others may already have the parent
                            if (broadcast) {
                                auto n = coordinator->broadcast (top, entry, size);

                                if (source == nullptr) {
                                    raddi::log::event (raddi::component::main, 0x21, entry->id, n);
                                }
                            }
                        }
                    }
                } catch (const std::bad_alloc &) {
                    SetEvent (::optimize);
                }
                break;
                        
            case raddi::db::classify:

                // ignore the ones we already processed recently

                if (!(inserted = coordinator->recent.insert (entry->id)))
                    break;

                // additional check against consensus size/proof for non-announcement entries
                {
                    std::size_t proof_size = 0;
                    if (auto proof = entry->proof (size, &proof_size)) {

                        if (!assess_extra_size_requirements (entry, size - proof_size - sizeof (raddi::entry), level)) {
                            break;
                        }

                        bool disconnect;
                        if (!assess_extra_proof_requirements (entry, proof, level, disconnect)) {
                            if (disconnect)
                                return false;
                            else
                                break;
                        }
                    }
                }

                // keep track on threads and basic channel traffic details
                // so that GUI apps can present meaningful info to the user
                
                if (settings.track_all_channels)
                    return true;
                
                // ensure data comming from other connections are current and not being a flood of historical playback
                //  - historic entries must be withing range allowed by temporary (?) extension
                //  - unsolicited entries must always be within general network propagation range (-10...+3 minutes)
                //  - TODO: make these constants a command-line option

                if (source) {
                    /*if (source->history_extension) {
                        if (raddi::older (entry->id.timestamp, source->history_extension - raddi::consensus::max_entry_age_allowed)) {
                            raddi::log::data (raddi::component::database, 0x16, entry->id, entry->id.timestamp,
                                                source->history_extension - raddi::consensus::max_entry_age_allowed,
                                                raddi::now () - source->history_extension + raddi::consensus::max_entry_age_allowed);
                            return true;
                        }
                    } else {
                        if (raddi::older (entry->id.timestamp, raddi::now () - raddi::consensus::max_entry_age_allowed)) {
                            raddi::log::data (raddi::component::database, 0x15, entry->id, entry->id.timestamp,
                                                raddi::now () - raddi::consensus::max_entry_age_allowed,
                                                raddi::consensus::max_entry_age_allowed);
                            return true;
                        }
                    }*/
                }

                // is thread, channel, entry (stream) or it's identity blacklisted
                //  - TODO: make function

                if (coordinator->blacklist.is_subscribed ({ top.channel, top.thread, entry->id, (raddi::eid) entry->id.identity })) {
                    coordinator->refused.insert (entry->id);
                    coordinator->detached.reject (entry->id);
                    return true;
                }

                // TODO: check against consensus for single user creating max number of channels/threads per time

                // is any of the apps subscribing to this entry' channel/thread/stream/identity
                //  - if not, then only distribute it to other connections (that participate in network propagation)
                //  - not testing for subscriptions if we are core node and store everything
                    
                if (source != nullptr
                        && !database->settings.store_everything
                        && !coordinator->subscriptions.is_subscribed ({ top.channel, top.thread, entry->id, (raddi::eid) entry->id.identity })) {

                    if (!coordinator->settings.network_propagation_participation) {
                        if (++source->unsolicited > coordinator->settings.max_allowed_unsolicited_entries)
                            return false;
                    }

                    // redistribute only if not old (would be rejected anyway)
                    //  - generaly old entries are comming back only on request

                    if (!old) {
                        coordinator->broadcast (top, entry, size);
                    }
                    return true;
                }

                // TODO: drop if old and connection hasn't extended acceptance range (haven't sent request or subscription)

                [[ fallthrough ]];

            case raddi::db::required:

                if (source != nullptr) {
                    // TODO: pass to spam-filtering plugins here
                }
                    
                bool exists = false;
                if (database->insert (entry, size, top, exists)) {
                    if (exists) {
                        
                        // FUTURE FEATURE: this might be 'stream'
                        //  - TODO: if author is subscribed to or channel allowed or something then save stream content to temp for app to handle
                        //  - TODO: database->insert will report errors for different content if content comparison is enabled, solve
                        //  - otherwise ignore duplicates

                    } else {

                        // remember as recent
                        //  - if 'insert' fails, we have already seen this one, don't broadcast then
                        //  - might have been already inserted into 'recent' in 'classify' case above

                        if (!inserted) {
                            inserted = coordinator->recent.insert (entry->id);
                        }

                        // process further only when seen for the first time

                        if (inserted) {

                            // redistribute to other connections if not old (would be rejected anyway)
                            //  - generaly old entries are comming back only on request

                            if (broadcast && !old) {
                                auto n = coordinator->broadcast (top, entry, size);

                                if (source == nullptr) {
                                    raddi::log::event (raddi::component::main, 0x21, entry->id, n);
                                }
                            }

                            // process detached entries whose parent has been inserted just now
                            //  - detached entries have already been validated
                            //  - TODO: evaluate for race conditions possibility on parallel insertions and embraces here:

                            return coordinator->detached.accept (entry->id, [source, nesting] (const std::uint8_t * data, std::size_t size) {
                                auto entry = reinterpret_cast <const raddi::entry *> (data);
                                raddi::log::note (raddi::component::database, 6, entry->id, entry->parent);
                                return embrace (source, entry, size, nesting + 1);
                            });
                        }
                    }
                } else {
                    // basically disk write problem or not enough memory
                    // database->insert also checks for valid entry size, but that should've been verified by now

                    // TODO: something like overview.set (L"fail...", 1);

                    SetEvent (::optimize);
                }
                break;
        }
        return true;
    }

    std::wstring DetermineDatabaseDirectory (raddi::log::scope scope, const wchar_t * option_database) {

        wchar_t path [32768];
        if (raddi::log::get_scope_path (scope, path)) {
            std::wcscat (path, raddi::defaults::data_subdir);
            CreateDirectory (path, NULL);

        } else {
            GetModuleFileName (NULL, path, sizeof path / sizeof path [0]);
            if (auto * end = std::wcsrchr (path, L'\\')) {
                *(end + 1) = L'\0';
            }
            // TODO: if it's not writable, find other place; same for log
        }

        if (option_database) {
            if ((std::wcslen (option_database) >= 4) && (option_database [0] == L'\\' || option_database [2] == L'\\')) {
                std::wcscpy (path, option_database); // use full path
            } else {
                std::wcscat (path, option_database); // append db name
            }
        } else {
            std::wcscat (path, L"database");
        }

        return path;
    }

    ULONG CALLBACK SuspendResumeCallbackRoutine (PVOID context, ULONG type, PVOID data) {
        // data == POWERBROADCAST_SETTING* when service, otherwise NULL
        switch (type) {
            case PBT_APMSUSPEND:
                raddi::log::event (0x09);
                coordinator->flush ();
                coordinator->disconnect ();
                break;
            case PBT_APMRESUMESUSPEND:
            case PBT_APMRESUMECRITICAL:
            case PBT_APMRESUMEAUTOMATIC:
                raddi::log::event (0x0A);

                // TODO: reset coordinator peer re-connection counters and parameters
                // TODO: more than one may come in succession, check with timer
                break;
        }
        return ERROR_SUCCESS;
    }

    struct report_service_stopped {
        ~report_service_stopped () {
            if (handle) {
                status.dwCurrentState = SERVICE_STOPPED;
                SetServiceStatus (handle, &status);
            }
        }
    };

    raddi::log::scope scope_from_status () {
        if (status.dwWin32ExitCode == NO_ERROR)
            return raddi::log::scope::machine;
        else
            return raddi::log::scope::user;
    }

    void WINAPI service (DWORD argc, LPWSTR * argw) {
        const auto onexit = report_service_stopped ();
        const auto scope = scope_from_status ();

        if (argc < 2) {
            argc = __argc;
            argw = __wargv;
        }

        raddi::log::display (option (argc, argw, L"display"));
        raddi::log::initialize (option (argc, argw, L"log"), raddi::defaults::log_subdir, L"node", scope);

        if (version == nullptr) {
            raddi::log::stop (0x01);
            status.dwWin32ExitCode = ERROR_FILE_CORRUPT;
            return;
        }

        raddi::log::event (0x01,
                           (unsigned long) HIWORD (version->dwProductVersionMS),
                           (unsigned long) LOWORD (version->dwProductVersionMS),
                           ARCHITECTURE, BUILD_TIMESTAMP, COMPILER);

        // load wininet.dll for error message table
        //  - while we use winhttp.dll the error messages are only in wininet.dll
        
        LoadLibrary (L"WININET");

        if (auto h = LoadLibrary (SQLITE3_DLL_NAME)) {
            // node does not use sqlite, but logs version number for sake of completeness
            if (auto p = reinterpret_cast <const char * (*) ()> (GetProcAddress (h, "sqlite3_libversion"))) {
                raddi::log::note (0x05, "sqlite3", p (), SQLITE3_DLL_TYPE);
            }
            FreeLibrary (h);
        }
        raddi::log::note (0x05, "liblzma", lzma_version_string (), GetModuleHandle (L"liblzma") ? L"dynamic" : L"static");
        raddi::log::note (0x05, "libsodium", sodium_version_string (), GetModuleHandle (L"libsodium") ? L"dynamic" : L"static");
        
#ifdef CRT_STATIC
        raddi::log::note (0x05, "vcruntime", CRT_STATIC, L"static");
#else
        // NOTE: also in HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\ (debug) \ x64 | x86 \ Version
        if (auto h = GetModuleHandle (L"VCRUNTIME140")) {
            if (auto info = GetModuleVersionInfo (h)) {
                wchar_t vs [32];
                std::swprintf (vs, sizeof vs / sizeof vs [0], L"%u.%u.%u",
                               (unsigned long) HIWORD (info->dwFileVersionMS),
                               (unsigned long) LOWORD (info->dwFileVersionMS),
                               (unsigned long) HIWORD (info->dwFileVersionLS));
                raddi::log::note (0x05, "vcruntime", vs, L"dynamic");
            }
        }
#endif
        struct {
            const char * name;
            int present;
        } cpu_features [] = {
#if defined (_M_ARM64)
            { "NEON", sodium_runtime_has_neon () },
            { "CRYPTO", sodium_runtime_has_armcrypto () },
#else
            { "SSE2", sodium_runtime_has_sse2 () },
            { "SSE3", sodium_runtime_has_sse3 () },
            { "SSSE3", sodium_runtime_has_ssse3 () },
            { "SSE4.1", sodium_runtime_has_sse41 () },
            { "AVX", sodium_runtime_has_avx () },
            { "AVX2", sodium_runtime_has_avx2 () },
            { "AVX512F", sodium_runtime_has_avx512f () },
            { "RDRAND", sodium_runtime_has_rdrand () },
            { "CLMUL", sodium_runtime_has_pclmul () },
            { "AES-NI", sodium_runtime_has_aesni () },
#endif
        };
        for (auto cpu_feature : cpu_features) {
            raddi::log::note (0x07, cpu_feature.name, (bool) cpu_feature.present);
        }

        raddi::log::event (0xF0, raddi::log::path);
        
        Optional <LONG> (L"ADVAPI32", "RegDisablePredefinedCacheEx");
        Optional <BOOL, DWORD> (L"KERNEL32", "SetSearchPathMode", 0x8001); // permanent safe search mode
#ifndef _WIN64
        Optional <BOOL, DWORD> (L"KERNEL32", "SetProcessDEPPolicy", 0x0003); // DEP enable without ATL thunking support
#endif
        if (status.dwWin32ExitCode == NO_ERROR) {
            handle = RegisterServiceCtrlHandlerEx (services [0].lpServiceName, handler, NULL);
            if (handle) {
                SetServiceStatus (handle, &status);
            } else {
                status.dwWin32ExitCode = GetLastError ();
                return;
            }
            raddi::log::display (L"disabled");
        } else {
            status.dwWin32ExitCode = 0;
        }

        if (scope == raddi::log::scope::user) {
            DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS callback = { SuspendResumeCallbackRoutine, NULL };
            Optional <HPOWERNOTIFY, HANDLE, DWORD> (L"USER32", "RegisterSuspendResumeNotification", &callback, DEVICE_NOTIFY_CALLBACK);
        }

        // overview
        //  - monitoring and control from other processes
        //
        raddi::instance overview (scope);

        if (overview.status != ERROR_SUCCESS) {
            raddi::log::error (3, overview.failure_point, raddi::log::api_error (overview.status));
        }
        overview.set (L"magic", raddi::protocol::magic);

        // node settings
        
        option (argc, argw, L"proof-complexity-requirements-adjustment", settings.proof_complexity_requirements_adjustment);

        // core/leaf
        //  - summary command-line parameters for main modes of operation
        //  - these only affect other separately configurable parameters below
        
        bool core = false;
        bool leaf = false;
        option (argc, argw, L"core", core);
        option (argc, argw, L"leaf", leaf);

        // database
        //  - discussion entries organized for fast search
        //
        raddi::db database (file::access::write,
                            DetermineDatabaseDirectory (scope, option (argc, argw, L"database")));

        if (database.connected ()) {
            overview.set (L"database", database.path);

            if (core) {
                database.settings.store_everything = true;
            }

            option (argc, argw, L"database-store-everything", database.settings.store_everything);
            option (argc, argw, L"database-backtrack-granularity", database.settings.backtrack_granularity);
            option (argc, argw, L"database-reinsertion-validation", database.settings.reinsertion_validation);
            option (argc, argw, L"database-xor-mask-size", database.settings.xor_mask_size);

            ::database = &database;
        } else {
            status.dwWin32ExitCode = GetLastError ();
            return;
        }

        // coordinator
        //  - management of adequate conection to the network
        //  - TODO: document following settings in doc/parameters.txt
        //
        raddi::coordinator coordinator (database);

        if (leaf) {
            settings.track_all_channels = false;
            coordinator.settings.network_propagation_participation = false;
            coordinator.settings.channels_synchronization_participation = false;
            coordinator.settings.min_core_connections = std::max (coordinator.settings.min_core_connections, raddi::defaults::coordinator_leaf_min_core_connections);
            coordinator.settings.max_core_connections = std::max (coordinator.settings.max_core_connections, raddi::defaults::coordinator_leaf_max_core_connections);
        }
        if (core) {
            coordinator.settings.full_database_downloads_allowed = true;
        }

        option (argc, argw, L"connections", coordinator.settings.connections);
        option (argc, argw, L"max-connections", coordinator.settings.max_connections);
        option (argc, argw, L"min-connections", coordinator.settings.min_connections);
        option (argc, argw, L"max-core-connections", coordinator.settings.max_core_connections);
        option (argc, argw, L"min-core-connections", coordinator.settings.min_core_connections);
        option (argc, argw, L"local", coordinator.settings.local_peers_only); // 'local-peers-only'?
        option (argc, argw, L"network-propagation-participation", coordinator.settings.network_propagation_participation);
        option (argc, argw, L"channels-synchronization-participation", coordinator.settings.channels_synchronization_participation);
        option (argc, argw, L"full-database-downloads", coordinator.settings.full_database_downloads_allowed);
        option (argc, argw, L"full-database-download-limit", coordinator.settings.full_database_download_limit);

        
        option (argc, argw, L"track-all-channels", settings.track_all_channels);
        option (argc, argw, L"keep-alive", coordinator.settings.keep_alive_period);

        // option (argc, argw, L"", coordinator.settings.announcement_sample_size);

        if (!ReserveServerMemory (coordinator.settings.max_connections)) {
            if (coordinator.settings.max_connections) {
                raddi::log::error (7, coordinator.settings.max_connections);
            }
        }

        ::coordinator = &coordinator;

        // local address cache
        //  - allocated here for construction/destruction control
        //
        LocalHosts local_address_cache;
        ::localhosts = &local_address_cache;

        // source
        //  - directory that clients places their requests and entries to
        //
        Source source (option (argc, argw, L"source"),
                       std::to_wstring (GetCurrentProcessId ()).c_str ());

        // thread pool
        //  - by default, the number of threads is limited; the average number of connections
        //    is then not expected to exceed roughly 1/8 way from 'connections' to 'max_connections'

        option (argc, argw, L"threads", workers);
        if (workers == 0) {
            workers = ((std::size_t) GetLogicalProcessorCount () * 15 + 5) / 10;
            if (auto limit = (coordinator.settings.max_connections / 8 + coordinator.settings.connections) / 32) {
                if (workers > limit) {
                    workers = limit;
                }
            } else {
                workers = 1;
            }
        }
        for (auto i = 0uL; i != workers; ++i) {
            if (auto h = CreateThread (NULL, 0, worker, reinterpret_cast <LPVOID> ((std::size_t) i), 0, NULL)) {
                CloseHandle (h);
                WaitForSingleObject (workclock, INFINITE);

            } else {
                raddi::log::error (4, i, workers);
                workers = i;
                break;
            }
        }
        if (workers == 0) {
            status.dwWin32ExitCode = GetLastError ();
        }
        
        overview.set (L"workers", workers);
        overview.set (L"broadcasting", 0u);
        overview.set (L"log", raddi::log::path);
        overview.set (L"source", source.path);

        // protocol parameters
        //  - protocol magic is "RADDI/1" by default

        if (auto protocol = option (argc, argw, L"protocol")) {
            auto length = std::max (std::wcslen (protocol), sizeof raddi::protocol::magic);
            if (auto n = WideCharToMultiByte (CP_UTF8, 0, protocol, (int) length,
                                              raddi::protocol::magic, sizeof raddi::protocol::magic, NULL, NULL)) {
                while (n != sizeof raddi::protocol::magic) {
                    raddi::protocol::magic [n++] = '\0';
                }
            }
        }

        if (auto aes = option (argc, argw, L"aes")) {
            using namespace raddi::protocol;

            static const struct {
                const wchar_t *     value;
                enum aes256gcm_mode mode;
            } map [] = {
                { L"aegis", aes256gcm_mode::force_aegis },
                { L"force-aegis", aes256gcm_mode::force_aegis },
                { L"gcm", aes256gcm_mode::force_gcm },
                { L"force-gcm", aes256gcm_mode::force_gcm },
                { L"force", aes256gcm_mode::forced },
                { L"forced", aes256gcm_mode::forced },
                { L"disable", aes256gcm_mode::disabled },
                { L"disabled", aes256gcm_mode::disabled },
                { L"auto", aes256gcm_mode::automatic },
                { L"automatic", aes256gcm_mode::automatic }
            };
            for (const auto & mapping : map) {
                if (!std::wcscmp (aes, mapping.value)) {
                    aes256gcm_mode = mapping.mode;
                    break;
                }
            }

            // verify HW AES capabilities

            if (aes256gcm_mode != aes256gcm_mode::disabled) {
                switch (aes256gcm_mode) {

                    // if forced particular AES version is not available revert to default

                    case aes256gcm_mode::force_gcm:
                        if (!crypto_aead_aes256gcm_is_available ()) {
                            aes256gcm_mode = aes256gcm_mode::disabled;
                            raddi::log::error (11, aes256gcm::name, xchacha20poly1305::name);
                        }
                        break;

                    case aes256gcm_mode::force_aegis:
                        if (!crypto_aead_aegis256_is_available ()) {
                            aes256gcm_mode = aes256gcm_mode::disabled;
                            raddi::log::error (11, aegis256::name, xchacha20poly1305::name);
                        }
                        break;

                    // when forcing AES in general and only one of is available, select that
                    //  - if neither, revert to default as above

                    case aes256gcm_mode::forced:
                        if (!crypto_aead_aegis256_is_available () && !crypto_aead_aes256gcm_is_available ()) {
                            aes256gcm_mode = aes256gcm_mode::disabled;
                            raddi::log::error (11, "AES", xchacha20poly1305::name);
                        } else {
                            if (crypto_aead_aegis256_is_available ()) {
                                aes256gcm_mode = aes256gcm_mode::force_aegis;
                                raddi::log::note (6, aegis256::name);
                            } else
                            if (crypto_aead_aes256gcm_is_available ()) {
                                aes256gcm_mode = aes256gcm_mode::force_gcm;
                                raddi::log::note (6, aes256gcm::name);
                            }
                        }
                        break;
                }
            }
        }

        // proxy server connections
        //  - SOCKS5t (Tor)
        
        raddi::socks5proxy = raddi::loopback_IPv4;
        raddi::socks5proxy.port = 0;

        if (auto addr = option (argc, argw, L"proxy")) {
            if (std::wcschr (addr, L'.') || std::wcschr (addr, L':')) {
                SOCKADDR_INET a;
                if (StringToAddress (a, addr)) {
                    raddi::socks5proxy = raddi::address (a);
                }
            } else {
                raddi::socks5proxy.port = (std::uint16_t) std::wcstoul (addr, nullptr, 10);
            }
            if (raddi::socks5proxy.port == 0) {
                raddi::socks5proxy.port = raddi::defaults::socks5t_port;
            }
        }

        // listeners
        //  - start listening for inbound connections

        if (options (argc, argw, L"listen", [&coordinator](const wchar_t * address) {
            if (std::wcscmp (address, L"off") != 0) {
                if (!coordinator.listen (address)) {
                    raddi::log::error (raddi::component::server, 10, address);
                }
            }
        }) == 0) {
            coordinator.listen (raddi::defaults::coordinator_listening_port);
        }

        // discovery
        //  - local peer discovery

        if (options (argc, argw, L"discovery", [&coordinator](const wchar_t * address) {
            if (std::wcscmp (address, L"off") != 0) {
                if (!coordinator.discovery (address)) {
                    raddi::log::error (raddi::component::server, 10, address);
                }
            }
        }) == 0) {
            coordinator.discovery (raddi::defaults::coordinator_discovery_port);
        }


        // bootstrap
        //  - retrieves IP addresses from various sources:
        //     - DNS - queries DNS for addresses (see syntax in dns.hpp or parameters.txt)
        //     - HTTP/HTTPS bootstrap files - downloads file, parses IP:port per line
        //  - TODO: consider adding FTP

        bool boostrap_default = false;
        std::vector <wchar_t *> bootstraps_dns;
        std::vector <wchar_t *> bootstraps_http;

        if (options (argc, argw, L"bootstrap", [&bootstraps_dns, &bootstraps_http] (wchar_t * url) {
            if (std::wcscmp (url, L"off") != 0) {

                if (std::wcsncmp (url, L"dns:", 4) == 0) {
                    bootstraps_dns.push_back (url);
                } else
                if (std::wcsncmp (url, L"http://", 7) == 0) {
                    bootstraps_http.push_back (url);
                } else
                if (std::wcsncmp (url, L"https://", 8) == 0) {
                    bootstraps_http.push_back (url);
                } else {
                    raddi::log::error (raddi::component::main, 0x20,
                                       url, raddi::log::api_error (ERROR_WINHTTP_UNRECOGNIZED_SCHEME));
                }
            }
        }) == 0) {

            // if known core node list is empty, and no explicit boostraps provided, try fetching default
            if (coordinator.empty (raddi::core_nodes)) {
                boostrap_default = true;
            }
        }

        if (boostrap_default || !bootstraps_dns.empty ()) {
            try {
                dns = new Dns;
                if (boostrap_default) {
                    dns->resolve (&coordinator, Dns::Type::A, L"443.raddi.net", 443);
                    dns->resolve (&coordinator, Dns::Type::A, L"44303.raddi.net", raddi::defaults::coordinator_listening_port);

                    if (IsWindowsVistaOrGreater ()) {
                        dns->resolve (&coordinator, Dns::Type::AAAA, L"443.raddi.net", 443);
                        dns->resolve (&coordinator, Dns::Type::AAAA, L"44303.raddi.net", raddi::defaults::coordinator_listening_port);
                    }
                }
                for (auto record : bootstraps_dns) {
                    dns->resolve (&coordinator, record, raddi::defaults::coordinator_listening_port);
                }
            } catch (const std::bad_alloc &) {
                // TODO: raddi::log::error (5, i, 0, x.what ());?
            }

            bootstraps_dns.clear ();
            bootstraps_dns.shrink_to_fit ();
        }

        if (!bootstraps_http.empty ()) {

            std::wstring proxy_ua_string;
            proxy_ua_string.reserve (9);
            proxy_ua_string += L"RADDI/";
            proxy_ua_string += std::to_wstring (HIWORD (version->dwProductVersionMS));
            proxy_ua_string += L'.';
            proxy_ua_string += std::to_wstring (LOWORD (version->dwProductVersionMS));

            try {
                downloader = new Download (option (argc, argw, L"bootstrap-proxy"),
                                           option (argc, argw, L"bootstrap-user-agent", proxy_ua_string.c_str ()));
                for (auto url : bootstraps_http) {
                    downloader->download (url, &coordinator);
                }
            } catch (const std::bad_alloc &) {
                // TODO: raddi::log::error (5, i, 0, x.what ());?
            } catch (...) {
                // probably failed to initialize WinInet, already reported
            }

            bootstraps_http.clear ();
            bootstraps_http.shrink_to_fit ();
        }

        // request time resynchronization
        //  - TODO: add some event message that this is happening

        std::system ("w32tm /resync /rediscover /nowait");

        // low memory optimizations

        HANDLE lowmemorynotification = CreateMemoryResourceNotification (LowMemoryResourceNotification);
        HANDLE lowmemorynotificationtimer = CreateWaitableTimer (NULL, FALSE, NULL);

        // go
        //  - all cleanups are scheduled to this low-priority low-utilization thread

        if (handle) {
            status.dwCurrentState = SERVICE_RUNNING;
            SetServiceStatus (handle, &status);
        }

        HANDLE events [] = { // TODO: would be nice to abstract WaitForMultipleObjects indexes into names
            terminating,
            disconnected,
            CreateWaitableTimer (NULL, FALSE, NULL), // midnight "I'm alive" report event
            CreateWaitableTimer (NULL, FALSE, NULL), // connections keep-alive ticking
            lowmemorynotification,
            optimize,
            CreateWaitableTimer (NULL, FALSE, NULL), // database disk flush

            CreateWaitableTimer (NULL, FALSE, NULL), // connections status
        };

        ScheduleTimerToLocalMidnight (events [2], +10'000'0);

        if (coordinator.settings.keep_alive_period) {
            ScheduleWaitableTimer (events [3], LONGLONG (coordinator.settings.keep_alive_period) * 1'000'0LL);
        }

        SetPeriodicWaitableTimer (events [6], database.settings.disk_flush_interval);
        ScheduleWaitableTimer (events [7], 60 * 60'000'000'0uLL);
        SetEvent (optimize);

        coordinator.start ();
        SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);

        bool initial_optimize = true;
        bool running = true;
        do {
            switch (WaitForMultipleObjects (sizeof events / sizeof events [0], events, FALSE, 1000)) {

                // failure
                //  - an event was closed unexpectedly, this is internal error, rough handling is enough
                //
                case WAIT_FAILED:
                    raddi::log::stop (4);
                    running = false;
                    terminate ();
                    Sleep (1000);

                    [[ fallthrough ]];

                // terminating
                //  - service is going to terminate (first signal) or all workers finished (second signal)
                //
                case WAIT_OBJECT_0 + 0:
                    running = false;
                    source.stop ();
                    coordinator.flush ();
                    break;

                // disconnected
                //  - one or more connections has disconnected in the past, low-priority clean up
                //
                case WAIT_OBJECT_0 + 1:
                    if (running) {
                        coordinator.sweep ();
                    }
                    goto case_WAIT_TIMEOUT;

                // midnight
                //  - report that the node process is still alive
                //  - TODO: show some stats, data transfered, connections, etc.
                //
                case WAIT_OBJECT_0 + 2: {
                    SYSTEMTIME st;
                    GetLocalTime (&st);
                    raddi::log::event (0x03, st);
                    ScheduleTimerToLocalMidnight (events [2], 10'000'0);
                } break;

                // keep-alive
                //  - when nothing is received within a specified period
                //    the 0x0000 token is sent, the other side should reply with 0xFFFF token
                //  - scheduling the timer to signal for nearest keep-alive timeout
                //
                case WAIT_OBJECT_0 + 3:
                    ScheduleWaitableTimer (events [3], 10 * coordinator.keepalive ());
                    goto case_WAIT_TIMEOUT;

                // lowmemorynotification(timer)
                //  - event[4] is either event or timer; this alternates when the one waited-on fires
                //  - if low memory event fires, we schedule timer for next try
                //     - and fall through to memory optmization case
                //  - elapsed timer is replaced with the event to catch next low memory state
                //    which may be already (or still) in effect
                //
                case WAIT_OBJECT_0 + 4:
                    if (events [4] == lowmemorynotification) {
                        events [4] = lowmemorynotificationtimer;
                        ScheduleWaitableTimer (lowmemorynotificationtimer, 20 * 10'000'000LL); // 20s
                        
                        [[ fallthrough ]];
                        
                // optimize
                //  - low memory state hit in worker or detected by service handler
                // 
                case WAIT_OBJECT_0 + 5:
                        if (!initial_optimize) {
                            raddi::log::event (0x04);
                        }
                        if (IsWindows8Point1OrGreater ()) {
                            HeapSetInformation (NULL, HeapOptimizeResources, NULL, 0);
                        }

                        CompactServerMemory ();
                        database.optimize (true);
                        database.flush ();
                        coordinator.optimize ();

                        if (IsWindows8Point1OrGreater ()) {
                            HeapSetInformation (NULL, HeapOptimizeResources, NULL, 0);
                        }
                        SetProcessWorkingSetSize (GetCurrentProcess (), (SIZE_T) -1, (SIZE_T) -1);
                        initial_optimize = false;
                    } else {
                        events [4] = lowmemorynotification;
                    }
                    break;

                // database disk flush
                //  - mostly so that winapi writes metadata and reader's ReadDirectoryChangesW fires
                //
                case WAIT_OBJECT_0 + 6:
                    database.flush ();
                    break;

                // status
                //  - for debugging purposes, displays status of the connections
                //
                case WAIT_OBJECT_0 + 7:
                    coordinator.status ();
                    ScheduleWaitableTimer (events [7], 60 * 60'000'000'0uLL);
                    break;

                case WAIT_TIMEOUT:
                case_WAIT_TIMEOUT:
                    
                    if (running) {
                        try {
                            coordinator ();
                            localhosts->refresh ();
                            database.optimize ();

                        } catch (const std::bad_alloc &) {
                            SetEvent (optimize);
                        } catch (const std::exception &) {
                            // ??
                        }
                    }

                    if (downloader) {
                        if (downloader->done ()) {
                            delete downloader;
                            downloader = nullptr;
                        }
                    }

                    // update status entries
                    //  - TODO: entries processed, accepted, rejected, transmitted (per entry & per transmission)
                    //  - TODO: list of connections
                    //  - TODO: list of currently open database shards
                    //  - TODO: some database stats?

                    overview.set (L"connections", coordinator.active ());
                    overview.set (L"transmitted", Transmitter::total.bytes);
                    overview.set (L"received", Receiver::total.bytes);
                    overview.set (L"accepted", Listener::total.accepted);
                    overview.set (L"rejected", Listener::total.rejected);
                    overview.set (L"processed", source.total);

                    overview.set (L"detached", coordinator.detached.size ().bytes);
                    overview.set (L"detached highwater", coordinator.detached.highwater.bytes);

                    auto stats = database.stats ();
                    overview.set (L"shards", stats.shards.active);
                    overview.set (L"cache", stats.rows); // TODO: different name?

                    overview.set (L"broadcasting", (unsigned int) (running && source.start () && coordinator.broadcasting ()));
            }

            overview.set (L"heartbeat", raddi::microtimestamp ());
        } while (InterlockedCompareExchange (&workers, 0, 0) != 0);
        SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_NORMAL);

        for (auto & event : events) {
            CloseHandle (event);
        }

        ::localhosts = nullptr;
        ::coordinator = nullptr;
        ::database = nullptr;
        return;
    }

    void terminate () {
        SetEvent (terminating);
        coordinator->terminate ();
        
        delete dns;
        dns = nullptr;

        delete downloader;
        downloader = nullptr;

        std::size_t n = workers;
        for (std::size_t i = 0; i != n; ++i) {
            PostQueuedCompletionStatus (iocp, 0, 0, NULL);
        }
    }

    DWORD WINAPI worker (LPVOID i_) {
        const auto i = (int) (std::ptrdiff_t) i_;
        const auto id = GetCurrentThreadId ();

        raddi::log::note (3, i, id);
        ReleaseSemaphore (workclock, 1, NULL);

        BOOL         success;
        DWORD        n = 0;
        ULONG_PTR    key = NULL;
        OVERLAPPED * overlapped = NULL;

        do {
            success = GetQueuedCompletionStatus (iocp, &n, &key, &overlapped, INFINITE);
            if (overlapped) {
                try {
                    static_cast <Overlapped *> (overlapped)->completion (success, n);
                    
                } catch (const std::bad_alloc & x) {
                    raddi::log::error (5, i, id, x.what ());
                    SetEvent (optimize);
                } catch (const std::exception & x) {
                    raddi::log::error (6, i, id, x.what ());
                } catch (...) {
                    raddi::log::stop (7, i, id);
                    terminate ();
                }
            }
        } while (overlapped || key || n);

        raddi::log::note (4, i, id);
        
        if (InterlockedDecrement (&workers) == 0) {

            // last worker exiting
            //  - signal main thread again so it does not wait for timeout and exits

            SetEvent (terminating);
        }
        return 0;
    }

    DWORD WINAPI handler (DWORD code, DWORD event, LPVOID data, LPVOID context) {
        if (code != SERVICE_CONTROL_INTERROGATE) {
            raddi::log::event (7, raddi::log::rsrc_string (0x00100 + code));
        }
        switch (code) {
            case SERVICE_CONTROL_PRESHUTDOWN:
            case SERVICE_CONTROL_SHUTDOWN:
            case SERVICE_CONTROL_STOP:
                status.dwCurrentState = SERVICE_STOP_PENDING;
                SetServiceStatus (handle, &status);
                terminate ();
                break;

            case SERVICE_CONTROL_INTERROGATE:
                SetServiceStatus (handle, &status);
                break;

            case SERVICE_CONTROL_TIMECHANGE:
                if (auto change = static_cast <SERVICE_TIMECHANGE_INFO *> (data)) {
                    raddi::log::event (2, *(FILETIME*) &change->liOldTime, *(FILETIME*) &change->liNewTime);
                }
                break;
            case SERVICE_CONTROL_LOWRESOURCES:
            case SERVICE_CONTROL_SYSTEMLOWRESOURCES:
                SetEvent (optimize);
                break;

            case SERVICE_CONTROL_POWEREVENT:
                return SuspendResumeCallbackRoutine (context, event, data);

            default:
                return ERROR_CALL_NOT_IMPLEMENTED;
        }
        return NO_ERROR;
    }

    BOOL WINAPI console (DWORD code) {
        raddi::log::event (8, raddi::log::rsrc_string (0x00010 + code));
        
        switch (code) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                terminate ();
                Sleep (6000); // returning from console handler early will get the process killed
                return true;
            default:
                return false;
        }
    }
}

#ifdef CRT_STATIC
extern "C" char * __cdecl __unDName (void *, const void *, int, void *, void *, unsigned short) {
    return nullptr;
}
extern "C" LCID __cdecl __acrt_DownlevelLocaleNameToLCID (LPCWSTR localeName) {
    return 0;
}
extern "C" int __cdecl __acrt_DownlevelLCIDToLocaleName (
    LCID   lcid,
    LPWSTR outLocaleName,
    int    cchLocaleName
) {
    return 0;
}
#endif
