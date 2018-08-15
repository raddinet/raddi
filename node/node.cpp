#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <winhttp.h>
#include <mstcpip.h>
#include <mswsock.h>
#include <shlobj.h>

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
#include "download.h"
#include "localhosts.h"

#include "../core/raddi_defaults.h"
#include "../core/raddi_connection.h"
#include "../core/raddi_coordinator.h"
#include "../core/raddi_command.h"
#include "../core/raddi_request.h"
#include "../core/raddi_noticed.h"
#include "../core/raddi_consensus.h"
#include "../core/raddi_instance.h"

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

    const VS_FIXEDFILEINFO * const version = GetCurrentProcessVersionInfo ();
    const SERVICE_TABLE_ENTRY services [] = {
        { const_cast <LPWSTR> (TEXT ("raddi")), service },
        { NULL, NULL }
    };
    
    void terminate ();
    bool embrace (raddi::connection * source, const raddi::entry * entry, std::size_t size, std::size_t nesting = 0);
    bool assess_proof_requirements (const void * entry, std::size_t size, bool & disconnect);

    std::size_t          workers = 0;
    raddi::db *          database = nullptr;
    raddi::coordinator * coordinator = nullptr;
    LocalHosts *         localhosts = nullptr;
}

int wmain (int argc, wchar_t ** argw) {
    SetErrorMode (0x8007);
    SetDllDirectoryW (L"");
    RegDisablePredefinedCache ();

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

        if (!StartServiceCtrlDispatcher (services)) {

            status.dwWin32ExitCode = GetLastError ();
            if (status.dwWin32ExitCode == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                SetConsoleCtrlHandler (console, true);

                argw [0] = services [0].lpServiceName;
                service (argc, argw);
            }
        }

        WSACleanup ();

        if (handle) {
            status.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus (handle, &status);
        }
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

bool raddi::discovery::is_local (const raddi::address & a) {
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
bool raddi::connection::head (const raddi::protocol::keyset * peer) {
    if (::localhosts->contains (this->peer) || ::coordinator->reflecting (peer)) {
        this->report (raddi::log::level::note, 3);
        return false;
    }
    if (::coordinator->reciprocal (this)) {
        this->report (raddi::log::level::note, 5);
        return false;
    }

    if (auto ee = this->proposal->accept (peer)) {

        // replace proposal with encryption
        delete this->proposal;
        this->encryption = ee;

        this->report (raddi::log::level::event, 4, ee->name ());

        // this may also call 'send' thus we need to have the 'encryption' above already set
        ::coordinator->established (this);
        return true;
    } else
        return false;
}
bool raddi::connection::message (const unsigned char * data, std::size_t size) {
    if (size >= sizeof (raddi::entry) + raddi::proof::min_size) {
        if (raddi::entry::validate (data, size)) {

            bool disconnect;
            if (assess_proof_requirements (data, size, disconnect)) {
                return embrace (this, reinterpret_cast <const raddi::entry *> (data), size);
            } else
                return disconnect;

        } else
            return false;
    } else
        return ::coordinator->process (data, size, this);
};

void DownloadedCallback (const SOCKADDR_INET & address) {
    ::coordinator->add (raddi::core_nodes, address);
}
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
                // TODO: settings: allow log level changing, minimal/maximal allowed log level
                if (size >= sizeof (raddi::log::level)) {
                    raddi::log::settings::level = *reinterpret_cast <const raddi::log::level *> (cmd->content ());
                }
                break;
            case raddi::command::type::set_display_level:
                // TODO: settings: same as above
                if (size >= sizeof (raddi::log::level)) {
                    raddi::log::settings::display = *reinterpret_cast <const raddi::log::level *> (cmd->content ());
                }
                break;

            case raddi::command::type::optimize:
                SetEvent (optimize);
                break;

            // peers

            case raddi::command::type::add_peer:
            case raddi::command::type::rem_peer:
            case raddi::command::type::ban_peer:
            case raddi::command::type::unban_peer:
            case raddi::command::type::connect_peer:
            case raddi::command::type::add_core_peer:
                if ((coordinator != nullptr) && (size >= sizeof (raddi::address))) {
                    const auto & address = *reinterpret_cast <const raddi::address *> (cmd->content ());
                    if (address.valid ()) {

                        switch (cmd->type) {
                            case raddi::command::type::add_peer:
                                coordinator->add (raddi::level::announced_nodes, address);
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
                                coordinator->add (raddi::level::announced_nodes, address);
                                coordinator->connect (address);
                                break;

                            case raddi::command::type::add_core_peer:// TODO: option to allow/disallow adding core peers
                                coordinator->add (raddi::level::core_nodes, address);
                                break;
                        }
                    }
                }
                break;

            // data

            case raddi::command::type::download:
                if ((coordinator != nullptr) && (size >= sizeof (raddi::request::download))) {
                    coordinator->broadcast (raddi::request::type::download,
                                            cmd->content (), sizeof (raddi::request::download));
                }
                break;

            case raddi::command::type::erase:
            case raddi::command::type::erase_thorough:
                if ((database != nullptr) && (size >= sizeof (raddi::eid))) {
                    const auto & entry = *reinterpret_cast <const raddi::eid *> (cmd->content ());

                    // TODO: if option erase allowed...

                    if (database->erase (entry, (cmd->type == raddi::command::type::erase_thorough) ? true : false)) {
                        // TODO: report success
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
                if ((coordinator != nullptr) && (size >= sizeof (raddi::command::subscription))) {
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
        raddi::log::event (raddi::component::main, 8, cmd->type, x.what ());
        return false;
    }
}

namespace {

    // assess_proof_requirements
    //  - checks if proof parameters match minimum required parameters for that entry type,
    //    optionally adjusted by user through a command-line parameter
    //
    bool assess_proof_requirements (const void * data, std::size_t size, bool & disconnect) {
        const auto entry = reinterpret_cast <const raddi::entry *> (data);
        const auto proof = entry->proof (size);

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

    // TODO: move to 'raddi::node::insert' where 'node' will contain database, coordinator, glue functions and options loading
    //  - and only Win32 stuff will remain in node.cpp

    bool embrace (raddi::connection * source, const raddi::entry * entry, std::size_t size, std::size_t nesting) {
        const bool broadcast = (nesting == 0); // don't broadcast if called as part of detached reordering nesting, already have
        const bool old = raddi::older (entry->id.timestamp, raddi::now () - raddi::consensus::max_entry_age_allowed);
        bool inserted = false;

        raddi::db::root top;
        switch (database->assess (entry, size, &top)) {

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

    std::wstring DetermineDatabaseDirectory (bool global, const wchar_t * option_database) {
 
        // CSIDL_COMMON_APPDATA = C:\\ProgramData == global
        // CSIDL_LOCAL_APPDATA = C:\\Users\\<name>\\AppData\\Local == local

        wchar_t path [32768];
        if (SHGetFolderPath (NULL, global ? CSIDL_COMMON_APPDATA : CSIDL_LOCAL_APPDATA, NULL, 0, path) == S_OK) {
            std::wcscat (path, L"\\RADDI.net\\");
            CreateDirectory (path, NULL);
        } else {
            GetModuleFileName (NULL, path, sizeof path / sizeof path [0]);
            if (auto * end = std::wcsrchr (path, L'\\')) {
                *(end + 1) = L'\0';
            }
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

    void WINAPI service (DWORD argc, LPWSTR * argw) {
        const bool global = (status.dwWin32ExitCode == NO_ERROR);

        raddi::log::display (option (argc, argw, L"display"));
        raddi::log::initialize (option (argc, argw, L"log"), L"\\RADDI.net\\", L"node", global);

        if (version == nullptr) {
            raddi::log::stop (0x01);
            status.dwWin32ExitCode = ERROR_FILE_CORRUPT;
            return;
        }

        raddi::log::event (0x01,
                           (unsigned long) HIWORD (version->dwProductVersionMS),
                           (unsigned long) LOWORD (version->dwProductVersionMS),
                           ARCHITECTURE, BUILD_TIMESTAMP);

        if (auto h = LoadLibrary (L"sqlite3.dll")) {
            // node does not use sqlite, but logs version number for sake of completeness
            if (auto p = reinterpret_cast <const char * (*) ()> (GetProcAddress (h, "sqlite3_libversion"))) {
                raddi::log::note (0x05, "sqlite3", p (), L"dynamic");
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

        // overview
        //  - monitoring and control from other processes
        //
        raddi::instance overview (global);

        if (overview.status != ERROR_SUCCESS) {
            raddi::log::error (3, overview.failure_point, raddi::log::api_error (overview.status));
        }

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
                            DetermineDatabaseDirectory (global, option (argc, argw, L"database")));

        if (database.connected ()) {
            overview.set (L"database", database.path);

            if (core) {
                database.settings.store_everything = true;
            }

            option (argc, argw, L"database-store-everything", database.settings.store_everything);
            option (argc, argw, L"database-backtrack-granularity", database.settings.backtrack_granularity);
            option (argc, argw, L"database-reinsertion-validation", database.settings.reinsertion_validation);
            option (argc, argw, L"database-xor-mask-size", database.settings.xor_mask_size);

            // TODO: remember largest EID (time of last shutdown) and current time, to ask for resync among core nodes

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
            coordinator.settings.network_propagation_participation = false;
            coordinator.settings.channels_synchronization_participation = false;
            coordinator.settings.min_core_connections += raddi::defaults::coordinator_additional_leaf_min_core_connections;
            coordinator.settings.max_core_connections += raddi::defaults::coordinator_additional_leaf_max_core_connections;
        }
        if (core) {
            coordinator.settings.full_database_downloads_allowed = true;
        }

        option (argc, argw, L"connections", coordinator.settings.connections);
        option (argc, argw, L"max-connections", coordinator.settings.max_connections);
        option (argc, argw, L"min-connections", coordinator.settings.min_connections);
        option (argc, argw, L"max-core-connections", coordinator.settings.max_core_connections);
        option (argc, argw, L"min-core-connections", coordinator.settings.min_core_connections);
        option (argc, argw, L"network-propagation-participation", coordinator.settings.network_propagation_participation);
        option (argc, argw, L"channels-synchronization-participation", coordinator.settings.channels_synchronization_participation);
        option (argc, argw, L"full-database-downloads", coordinator.settings.full_database_downloads_allowed);
        option (argc, argw, L"full-database-download-limit", coordinator.settings.full_database_download_limit);

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
            workers = (GetLogicalProcessorCount () * 15 + 5) / 10;
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
            static const struct {
                const wchar_t * value;
                enum raddi::protocol::aes256gcm_mode mode;
            } map [] = {
                { L"force", raddi::protocol::aes256gcm_mode::forced },
                { L"forced", raddi::protocol::aes256gcm_mode::forced },
                { L"disable", raddi::protocol::aes256gcm_mode::disabled },
                { L"disabled", raddi::protocol::aes256gcm_mode::disabled },
                { L"auto", raddi::protocol::aes256gcm_mode::automatic },
                { L"automatic", raddi::protocol::aes256gcm_mode::automatic }
            };
            for (const auto & mapping : map)
                if (!std::wcscmp (aes, mapping.value)) {
                    raddi::protocol::aes256gcm_mode = mapping.mode;
                    break;
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

        // http client
        //  - for bootstrap, and possible future features

        if (WinHttpCheckPlatform ()) {
            std::wstring proxy_ua_string;
            proxy_ua_string.reserve (9);
            proxy_ua_string += L"RADDI/";
            proxy_ua_string += std::to_wstring (HIWORD (version->dwProductVersionMS));
            proxy_ua_string += L'.';
            proxy_ua_string += std::to_wstring (LOWORD (version->dwProductVersionMS));

            if (InitializeDownload (option (argc, argw, L"bootstrap-proxy"),
                                    option (argc, argw, L"bootstrap-user-agent", proxy_ua_string.c_str ()))) {

                if (options (argc, argw, L"bootstrap", [] (wchar_t * url) {
                    if (std::wcscmp (url, L"off") != 0) {
                        Download (url);
                    }
                }) == 0) {

                    // no known core node? then download list from preconfigured URL
                    if (coordinator.empty (raddi::core_nodes)) {
                        wchar_t url [sizeof raddi::defaults::bootstrap_url / sizeof raddi::defaults::bootstrap_url [0]];
                        std::wcscpy (url, raddi::defaults::bootstrap_url);

                        // XP does not support modern SSL, remove 'S' from HTTPS, if HTTPS
                        if (!IsWindowsVistaOrGreater ()) {
                            if (url [4] == L's') {
                                std::wmemmove (&url [4], &url [5],
                                               sizeof raddi::defaults::bootstrap_url / sizeof raddi::defaults::bootstrap_url [0] - 5);
                            }
                        }
                        Download (url);
                    }
                }
            }
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
        ScheduleWaitableTimer (events [7], 60*60'000'000'0);
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
                //  - low memory detected in worker or by service handler
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
                    ScheduleWaitableTimer (events [7], 60*60'000'000'0);
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
        TerminateDownload ();
        coordinator->terminate ();

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
        DWORD        n;
        ULONG_PTR    key;
        OVERLAPPED * overlapped;

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

    DWORD WINAPI handler (DWORD code, DWORD event, LPVOID data, LPVOID) {
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
                    raddi::log::event (0x02, *(FILETIME*) &change->liOldTime, *(FILETIME*) &change->liNewTime);
                }
                break;
            case SERVICE_CONTROL_LOWRESOURCES:
            case SERVICE_CONTROL_SYSTEMLOWRESOURCES:
                SetEvent (optimize);
                break;

            default:
                return ERROR_CALL_NOT_IMPLEMENTED;
        }
        return NO_ERROR;
    }

    BOOL WINAPI console (DWORD code) {
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
