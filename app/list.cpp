#include "list.h"
#include "node.h"
#include "data.h"
#include "appapi.h"
#include "resolver.h"
#include "window.h"

#pragma warning (disable:26454) // -1u warnings

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern Node connection;
extern Data database;
extern Resolver resolver;

ListVirtual::ListVirtual (const WindowPublic * parent, UINT id)
    : hWnd (CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"",
                            WS_CHILD | WS_CLIPSIBLINGS | LVS_REPORT | LVS_EDITLABELS | LVS_SHAREIMAGELISTS | LVS_OWNERDATA,// | LVS_NOCOLUMNHEADER,
                            0, 0, 0, 0, parent->hWnd, (HMENU) id, NULL, NULL)) {

    if (this->hWnd) {
        ListView_SetToolTips (this->hWnd, parent->hToolTip);
        ListView_SetExtendedListViewStyle (this->hWnd, LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        try {
            LVCOLUMN column;
            column.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
            column.cx = 0;
            column.fmt = LVCFMT_LEFT;

            std::wstring text;
            for (auto i = 0u; (text = LoadString (0x50 + i)), !text.empty (); ++i) {
                column.iSubItem = i;
                column.pszText = const_cast <wchar_t *> (text.c_str ());
                ListView_InsertColumn (this->hWnd, i, &column);
            }

            ListView_SetColumnWidth (this->hWnd, 0, 128 * parent->dpi / 96);
            ListView_SetColumnWidth (this->hWnd, 4, 172 * parent->dpi / 96);
            ListView_SetColumnWidth (this->hWnd, 5, 128 * parent->dpi / 96);

        } catch (const std::bad_alloc &) {
            NMHDR nm = { this->hWnd, (UINT) id, (UINT) NM_OUTOFMEMORY };
            SendMessage (parent->hWnd, WM_NOTIFY, id, (LPARAM) & nm);
        } catch (...) {
            // ???
        }
    }
}

LRESULT ListVirtual::OnNotify (NMHDR * nm) {
    switch (nm->code) {
        case LVN_GETEMPTYMARKUP:
            if (auto markup = reinterpret_cast <NMLVEMPTYMARKUP *> (nm)) {
                markup->dwFlags = EMF_CENTERED;
                if (LoadString ((HINSTANCE) & __ImageBase, 0x25, markup->szMarkup, sizeof markup->szMarkup / sizeof markup->szMarkup [0]))
                    return TRUE;
            }
            break;
    }
    return 0;
}



ListOfChannels::ListOfChannels (const WindowPublic * parent, UINT id)
    : ListVirtual (parent, id) {

    // columns?
    // preload data
}

ListOfIdentities::ListOfIdentities (const WindowPublic * parent, UINT id)
    : ListVirtual (parent, id) {

    // columns?
    // preload data
}

LRESULT ListOfChannels::Update (std::size_t n_threads, std::size_t n_channels) {
    // TODO: fetch all and recompute order (in worker thread?)
    if (ListView_GetItemCount (this->hWnd) != n_channels) {
        ListView_SetItemCountEx (this->hWnd, n_channels, LVSICF_NOSCROLL);
    }
    return 0;
}

LRESULT ListOfIdentities::Update (std::size_t n) {
    ListView_SetItemCountEx (this->hWnd, n, LVSICF_NOSCROLL);
    return 0;
}

LRESULT ListOfChannels::OnNotify (NMHDR * nm) {
    /*switch (nm->code) {
        case LVN_ODCACHEHINT:
            // auto hint = reinterpret_cast <NMLVCACHEHINT *> (nm);
            break;
        case LVN_ODFINDITEM:
            // auto find = reinterpret_cast <NMLVFINDITEM *> (nm);
            return -1; // not found

        case NM_RCLICK:
            if (auto activated = reinterpret_cast <NMITEMACTIVATE *> (nm)) {
            }
            break;
    }*/
    return this->ListVirtual::OnNotify (nm);
}

LRESULT ListOfIdentities::OnNotify (NMHDR * nm) {
    /*switch (nm->code) {
        case LVN_ODCACHEHINT:
            // auto hint = reinterpret_cast <NMLVCACHEHINT *> (nm);
            // data->cache
            // TODO: always cache item 0 and the last one - TODO: count accesses?
            // NOT USEFUL: we need to fetch all to sort them etc.
            break;

            // case LVN_INCREMENTALSEARCH:
        case LVN_ODFINDITEM:
            // auto find = reinterpret_cast <NMLVFINDITEM *> (nm);
            // return index
            return -1; // not found

        case NM_RCLICK:
            if (auto activated = reinterpret_cast <NMITEMACTIVATE *> (nm)) {
                // context menu:

                // refresh command

                // open (open channel in new view tab, start downloading content) - double click also
                // open author's channel

                // items: show/hide hidden, hide this, remove (just me, delete from computer)
                //  mod ops: ... ban, ban author

                // copy 'eid' to clipboard
            }
            break;

            //case LVN_BEGINLABELEDIT:
            //case LVN_ENDLABELEDIT:
              //  break;

        case LVN_ITEMACTIVATE:
            break;

        case LVN_GETDISPINFO:
            auto & item = reinterpret_cast <NMLVDISPINFO *> (nm)->item;
            switch (item.iSubItem) {
                case 0:
                    item.pszText = const_cast <LPWSTR> (L"abc");
                    break;
                case 4:
                    item.pszText = const_cast <LPWSTR> (L"012345678abcdef00-12345678");
                    break;
                case 5:
                    item.pszText = const_cast <LPWSTR> (L"2019/03/05 11:23 AM");
                    break;
            }
            break;
    }*/
    return this->ListVirtual::OnNotify (nm);
}


HWND List::Create (const WindowPublic * parent, UINT base, UINT id) {
    auto style = WS_CHILD | WS_CLIPSIBLINGS | LVS_REPORT | LVS_EDITLABELS | LVS_SHAREIMAGELISTS | LVS_NOCOLUMNHEADER;
    auto extra = LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;

    // LVS_EX_INFOTIP???
    // LVS_EX_LABELTIP
    // LVS_EX_TRANSPARENTBKGND ??
  
    if (auto h = CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"", style,
                                 0, 0, 0, 0, parent->hWnd, (HMENU) (base + id), NULL, NULL)) {
        
        ListView_SetExtendedListViewStyle (h, extra);
        ListView_SetToolTips (h, parent->hToolTip);

        SendMessage (h, WM_SETREDRAW, FALSE, 0);

        try {
            new List (h, id); // TODO: do we need this?

            LVCOLUMN column;
            column.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;// | LVCF_ORDER;
            column.cx = 0;
            column.fmt = LVCFMT_LEFT;

            std::wstring text;
            for (auto i = 0u; (text = LoadString (0x50 + i)), !text.empty (); ++i) {
                column.iSubItem = i;
                column.pszText = const_cast <wchar_t *> (text.c_str ());
                ListView_InsertColumn (h, i, &column);
            }

            ListView_SetColumnWidth (h, 0, 128 * parent->dpi / 96);
            ListView_SetColumnWidth (h, 4, 172 * parent->dpi / 96);
            ListView_SetColumnWidth (h, 5, 128 * parent->dpi / 96);
            // TODO: ListView_SetColumnOrderArray 


            try {
                database.lists.subs.query.bind (id);

                LVGROUP group;
                group.cbSize = sizeof group;
                group.mask = LVGF_HEADER | LVGF_GROUPID;
                group.state = LVGS_NORMAL;
                group.iGroupId = 0;
                group.pszHeader = const_cast <wchar_t *> (L"rest of them");

                ListView_InsertGroup (h, 0, &group);

                while (database.lists.subs.query.next ()) {
                    text = database.lists.subs.query.get <std::wstring> (1);

                    group.pszHeader = const_cast <wchar_t *> (text.c_str ());
                    group.iGroupId = database.lists.subs.query.get <int> (0);

                    // TODO: order? column?
                    ListView_InsertGroup (h, -1, &group);
                }

                database.lists.data.query.bind (id);

                LVITEM item;
                item.mask = LVIF_TEXT | LVIF_GROUPID;
                item.iItem = 0;
                item.iSubItem = 0;
                item.pszText = const_cast <wchar_t *> (L"default");
                item.iGroupId = 0;

                while (database.lists.data.query.next ()) {
                    text = database.lists.data.query.get <std::wstring> (2);

                    try {
                        resolver.add (database.lists.data.query.get <SQLite::Blob> (1));
                    } catch (...) {
                        // TODO: for now we ignore BadBlobAssignmentException
                    }

                    item.pszText = const_cast <wchar_t *> (text.c_str ());
                    item.iGroupId = database.lists.data.query.get <int> (0);

                    ListView_InsertItem (h, &item);

                    // TODO: add remaining columns
                    // TODO: make first column dynamic (custom/resolved/original)
                }

            } catch (const SQLite::Exception &) {
                // ErrorBox (hWnd, raddi::log::level::error, 0x04, x.what ());
            }

            LVGROUPMETRICS metrics;
            metrics.cbSize = sizeof metrics;
            metrics.mask = LVGMF_BORDERSIZE;
            metrics.Top = 16;
            metrics.Left = 16;
            metrics.Right = 16;
            metrics.Bottom = 16; // TODO: update with DPI change

            ListView_SetGroupMetrics (h, &metrics);

            if (ListView_GetGroupCount (h) > 1) {
                ListView_EnableGroupView (h, TRUE);
            }

            SendMessage (h, WM_SETREDRAW, TRUE, 0);
            InvalidateRect (h, NULL, TRUE);
            return h;

        } catch (const std::bad_alloc &) {
            NMHDR nm = { h, (UINT) (base + id), (UINT) NM_OUTOFMEMORY };
            SendMessage (parent->hWnd, WM_NOTIFY, base + id, (LPARAM) &nm);
        } catch (...) {
            // ???
        }
        DestroyWindow (h);
    }
    return NULL;
}

List::List (HWND hWnd, UINT index)
    : hWnd (hWnd)
    , index (index) {
    
    SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR) this);
}

LRESULT List::OnNotify (NMHDR * nm) {
    auto data = reinterpret_cast <List *> (GetWindowLongPtr (nm->hwndFrom, GWLP_USERDATA));
    switch (nm->code) {

        case NM_RCLICK:
            if (auto activated = reinterpret_cast <NMITEMACTIVATE *> (nm)) {
                // context menu:
                MessageBeep (0);
                
            }
            break;

        /*case LVN_BEGINLABELEDIT:
        case LVN_ENDLABELEDIT:
            break;*/

        case LVN_ITEMACTIVATE:
            break;

        case LVN_GETEMPTYMARKUP:
            if (auto markup = reinterpret_cast <NMLVEMPTYMARKUP *> (nm)) {
                markup->dwFlags = EMF_CENTERED;
                if (LoadString ((HINSTANCE) &__ImageBase, 0x24,
                                markup->szMarkup, sizeof markup->szMarkup / sizeof markup->szMarkup [0]))
                    return TRUE;
            }
            break;
    }
    return 0;
}
