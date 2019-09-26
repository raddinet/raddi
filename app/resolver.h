#ifndef RESOLVER_H
#define RESOLVER_H

#include "../core/raddi.h"
#include "../common/log.h"
#include "../common/lock.h"
#include "../common/node.h"

#include <set>
#include <map>

// Rules
//  - set of rules, enabled/disabled
//  - TODO: moderators I follow globally or only per certain channels
//  - TODO: how many moderators must do, or endorse action, to apply (weight for each)
//
class Rules {
public:
    class Rule {
    public:
        bool enabled;

        std::map <raddi::iid, bool> identities; // TODO: mapping identities to behavior
    };

    // TODO: rules set?
    // TODO: need resolved result for different set of Rules (different windows)
};

// Resolver
//  - background priority thread that resolves names for identities or channels
//    as they might have been changed during the existence
//  - uses global 'Node connection'
//  - TODO: right now using map/set, but often there'll be just one window, optimizing for it would be nice
//
class Resolver
    : virtual raddi::log::provider <raddi::component::main> {

    HANDLE thread = NULL;
    union {
        HANDLE events [2] = { NULL, NULL };
        struct {
            HANDLE process;
            HANDLE finish;
        };
    };

    DWORD  message = 0;

    struct results {
        lock lock;

        std::map <raddi::eid, std::wstring> initial;
        std::map <raddi::eid, std::map <unsigned short, std::wstring>> resolved;
    } results;

    struct {
        lock lock;
        std::map <HWND, Rules>  rules;
        std::set <HWND>         clear; // windows
        std::set <HWND>         remove; // controls

        std::map <raddi::eid, std::set <HWND>> add;
    } changes;

private:
    // IMPORTANT: only worker manipulates 'items'

    struct Window {
        HWND  hWnd = NULL;
        Rules rules;
    };
    std::vector <Window> windows;

    struct Registration {
        unsigned short window; // index into 'windows'
        unsigned short child; // ctrl ID

        bool operator < (Registration other) const {
            return (this->window < other.window)
                || (this->window == other.window && (this->child < other.child));
        }

        Registration () = default;
        Registration (const Registration &) = default;
        Registration (Resolver * resolver, HWND hCtrl)
            : window ((unsigned short) resolver->window_index (GetAncestor (hCtrl, GA_ROOT)))
            , child ((unsigned short) GetDlgCtrlID (hCtrl)) { }
    };
    std::map <raddi::eid, std::set <Registration>> items;

public:
    Resolver () : provider ("resolver") {
        this->windows.reserve (16);
    }

    // initialize
    //  - thread calling this will receive 'message' callback
    //
    bool initialize (DWORD message);
    void terminate ();

    // start
    //  - actually starts resolving (in background thread)
    //
    void start ();

    // advance
    //  - called by application when 'threads' table changes (contains also name-change entries)
    //
    void advance (enum Node::table, std::uint32_t shard, std::uint32_t limit); // TODO: type and range
    
    void advance () {
        SetEvent (this->process);
    }

    // add
    //  - registers entry for resolving and update to be reported to root of 'requester'
    //  - returns true only when added for the first time
    //
    bool add (HWND requester, const raddi::eid & entry);

    // get
    //  - retrieves resolved name and/or enqeues it to resolve for 'requester'
    //  - returns true if retrieved at least initial name immediately
    //
    bool get (const raddi::eid & entry, HWND requester, std::wstring *);

    bool get_original_title (const raddi::eid & entry, std::wstring *);

    // change_rules
    //  -
    //
    void change_rules (HWND window, const Rules &);

    // remove
    //  - all requests to resolve made by 'requester'
    //
    void remove (HWND requester);

    // clear
    //  - removes all requests to resolve made by child windows of 'parent'
    //  - called when windows is destroyed
    //
    void clear (HWND parent);

private:
    long worker () noexcept;
    void process_changes ();
    void clean_abandoned ();
    // void update_resolved (std::map <raddi::eid, resolved::texts> &);
    
    Window &    window (HWND hWnd);
    std::size_t window_index (HWND hWnd);
};

#endif