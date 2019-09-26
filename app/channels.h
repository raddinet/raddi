#ifndef CHANNELS_H
#define CHANNELS_H

#include "../core/raddi.h"
#include "../common/node.h"
#include <map>

class Window;

class ListOfChannels {
public:
    const HWND hWnd;
    const Window * parent;
    const Node::table table;

public:
    ListOfChannels (const Window * parent, UINT id, Node::table table);

    LRESULT Update (/*std::size_t n_threads, std::size_t n_channels*/);
    LRESULT OnNotify (NMHDR *);
    LRESULT OnDpiChange (long dpi);
    LRESULT OnContextMenu (const Window * parent, LONG x, LONG y);

    bool GetItemEid (int row, raddi::eid * out) const {
        auto i = this->cache.find (row);
        if (i != this->cache.end ()) {
            if (out) {
                *out = i->second.id;
            }
            return true;
        } else
            return false;
    }

protected:
    static constexpr auto MaxSize = 100'000'000u; // TODO: check if this still holds
private:
    struct CacheEntry { // TODO: Resolver::Result
        raddi::eid   id;
        std::wstring original; // connection.database.get
        std::wstring resolved; // TODO: either resolver or resolved in thread
        unsigned int threads = 0;
        unsigned int participants = 0;
        unsigned int posts_by_author = 0;
    };

    // cache
    //  - scrolling/sorting/filtering generates 'cache'
    //  - key (int) is listview row
    //
    std::map <int, CacheEntry> cache;

    // prefetch
    //  -
    //
    void prefetch (int row);
    void unsynchronized_prefetch (int row);

    template <Node::table>
    void unsynchronized_prefetch_original_name_by_index (int row);

    // TODO: Sort
    //  - per:
    //     - name,
    //     - subscribers/threads
    //     - for identities: pet threads created by author in network
    //  - if NOT sorting by RESOLVED (or final) name, sort immediately and request name from resolver ...or what???
    //     - resolver is still needed for user lists


    // sort
    //  - 1-based index of column to sort by
    //  - 0 means don't sort, use db table index
    //
    int sort = 0;
    
    // TODO: Searchbox as filter
    bool filter = false;

    // TODO: filtering:
    //  - active in last X months: either created, or has threads?
    //  - mark subscribed (and optionally boost them to the top) - blue color?
    //  - mark friends - green color?

    // temporary
    //  - used for dynamically contructed replies to LVN_GETDISPINFO
    //
    std::wstring temporary;
};

#endif
