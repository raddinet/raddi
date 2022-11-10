#include "../common/appapi.h"
#include "../common/node.h"
#include "../common/log.h"

#include "data.h"
#include "window.h"
#include "menus.h"
#include "lists.h"

#include <VersionHelpers.h>
#include <cwctype>
#include <set>

#pragma warning (disable:26454) // -1u warnings

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern Node connection;
extern Data database;
extern Design design;

namespace {

    // u82ws
    //  - converts UTF-8 to UTF-16 (wchar_t) string
    //
    std::wstring u82ws (const uint8_t * data, std::size_t size) {
        std::wstring result;
        if (data && size) {
            if (auto n = MultiByteToWideChar (CP_UTF8, 0, (LPCCH) data, (int) size, NULL, 0)) {
                result.resize (n);
                MultiByteToWideChar (CP_UTF8, 0, (LPCCH) data, (int) size, &result [0], n);
            };
        }
        return result;
    }

    // map identitifier to one of the two tables

    template <Node::table>
    struct DbTableMeta {};

    template <>
    struct DbTableMeta <Node::table::identities> {
        using layout = raddi::identity;
        static const bool reversed = true;

        static auto * table () {
            return connection.database->identities.get ();
        }
    };

    template <>
    struct DbTableMeta <Node::table::channels> {
        using layout = raddi::channel;
        static const bool reversed = false;

        static auto * table () {
            return connection.database->channels.get ();
        }
    };

}

ListOfChannels::ListOfChannels (const Window * parent, UINT id, Node::table table)
    : hWnd (CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"",
                            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | LVS_REPORT | LVS_EDITLABELS | LVS_SHAREIMAGELISTS | LVS_OWNERDATA,
                            0, 0, 0, 0, parent->hWnd, (HMENU) (std::uintptr_t) id, NULL, NULL))
    , parent (parent)
    , table (table) {

    if (this->hWnd) {
        ListView_SetToolTips (this->hWnd, parent->hToolTip);
        ListView_SetExtendedListViewStyle (this->hWnd, LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

        if (winver >= 6) {
            SetWindowSubclass (this->hWnd, ListView_CustomHeaderSubclassProcedure, 0, (DWORD_PTR) this->parent);
        }

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

            this->OnDpiChange (parent->dpi);
            this->Update ();
        } catch (const std::bad_alloc &) {
            ReportOutOfMemory (this->hWnd);
        } catch (...) {
            // ???
        }
    }
}

LRESULT ListOfChannels::OnDpiChange (long dpi) {

    // TODO: get from user settings

    ListView_SetColumnWidth (this->hWnd, 0, 128 * dpi / 96);
    ListView_SetColumnWidth (this->hWnd, 5, 128 * dpi / 96);
    ListView_SetColumnWidth (this->hWnd, 6, 48 * dpi / 96);

    switch (table) {
        case Node::table::identities:
            ListView_SetColumnWidth (this->hWnd, 4, 112 * dpi / 96);
            break;
        case Node::table::channels:
            ListView_SetColumnWidth (this->hWnd, 4, 172 * dpi / 96);
            break;
    }

    // debug:

    ListView_SetColumnWidth (this->hWnd, 1, 32 * dpi / 96);
    ListView_SetColumnWidth (this->hWnd, 2, 32 * dpi / 96);
    ListView_SetColumnWidth (this->hWnd, 3, 32 * dpi / 96);
    ListView_SetColumnWidth (this->hWnd, 7, 32 * dpi / 96);
    ListView_SetColumnWidth (this->hWnd, 8, 32 * dpi / 96);
    return 0;
}

LRESULT ListOfChannels::Update () {
    this->cache.clear ();

    std::uint64_t size = 0;

    if (connection.connected ()) {
        switch (this->table) {
            case Node::table::identities:
                size = DbTableMeta<Node::table::identities>::table ()->size ();
                break;
            case Node::table::channels:
                size = DbTableMeta<Node::table::channels>::table ()->size ();
                break;
        }
    }

    if (size > MaxSize) {
        size = MaxSize;
    }
    ListView_SetItemCountEx (this->hWnd, (WPARAM) size, LVSICF_NOSCROLL);
    return 0;
}

LRESULT ListOfChannels::OnNotify (NMHDR * nm) {
    switch (nm->code) {
        case LVN_ITEMACTIVATE:
            break;

        case LVN_GETEMPTYMARKUP:
            if (auto markup = reinterpret_cast <NMLVEMPTYMARKUP *> (nm)) {
                markup->dwFlags = EMF_CENTERED;
                if (LoadString ((HINSTANCE) &__ImageBase, 0x25, markup->szMarkup, sizeof markup->szMarkup / sizeof markup->szMarkup [0]))
                    return TRUE;
            }
            break;

        case NM_CUSTOMDRAW:
            break;

            // why this all?
            if (auto draw = reinterpret_cast <NMLVCUSTOMDRAW *> (nm)) {
                switch (draw->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT:
                        return CDRF_NOTIFYSUBITEMDRAW;

                    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:

                        // return CDRF_DODEFAULT;

                        // static bool reset_font = false; // can be static, GUI is single threaded
                        /*switch (draw->iSubItem) {
                            default:
                                if (reset_font) {
                                    reset_font = false;
                                    SelectObject (draw->nmcd.hdc, parent->fonts.text.handle);
                                    return CDRF_NEWFONT;
                                } else
                                    break;

                            case 0:
                                auto found = this->cache.find ((int) draw->nmcd.dwItemSpec);
                                if (found != this->cache.end ()) {
                                    if (database.names.is.query <int> (SQLite::Blob (found->second.id))) {
                                        SelectObject (draw->nmcd.hdc, parent->fonts.italic.handle);
                                        reset_font = true;
                                        return CDRF_NEWFONT;
                                    }
                                }
                        }*/

                        // TODO: fetch text from LVN_GETDISPINFO

                        DrawCompositedTextOptions options;
                        options.font = parent->fonts.text.handle;
                        options.theme = GetWindowTheme (nm->hwndFrom);
                        options.color = draw->clrText;

                        switch (draw->iSubItem) {
                            default:
                                break;

                            case 0:
                                auto found = this->cache.find ((int) draw->nmcd.dwItemSpec);
                                if (found != this->cache.end ()) {
                                    if (database.names.is.query <int> (SQLite::Blob (found->second.id))) {
                                        options.font = parent->fonts.italic.handle;
                                    }
                                }
                        }

                        // TODO: background color
                        if (draw->nmcd.uItemState & CDIS_SELECTED) { // etc...
                            SetDCBrushColor (draw->nmcd.hdc, 0x00004F);
                            FillRect (draw->nmcd.hdc, &draw->nmcd.rc, (HBRUSH) GetStockObject (DC_BRUSH));
                        }

                        DrawCompositedText (draw->nmcd.hdc, (LPWSTR) L"ABCD", DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, draw->nmcd.rc, &options);
                        return CDRF_SKIPDEFAULT;
                }
            }
            break;

        case LVN_ODCACHEHINT:
            if (auto hint = reinterpret_cast <NMLVCACHEHINT *> (nm)) {
                // TODO: query to worker thread to compute: resolved name and counters
                if (sort || filter) {
                    

                } else {
                    if (this->cache.size () > 16u * (hint->iTo - hint->iFrom + 1)) {
                        this->cache.clear ();
                    }
                    try {
                        immutability guard (connection.lock);

                        for (int i = hint->iFrom; i != hint->iTo + 1; ++i) {
                            this->unsynchronized_prefetch (i);
                        }
                    } catch (const std::bad_alloc &) {
                        ReportOutOfMemory (this->hWnd);
                    }
                }
            }
            break;
        case LVN_GETDISPINFO:
            if (auto info = reinterpret_cast <NMLVDISPINFO *> (nm)) {
                auto & item = info->item;

                if (!this->cache.count (item.iItem)) {
                    this->prefetch (item.iItem);
                }

                auto found = this->cache.find (item.iItem);
                if (found != this->cache.end ()) {

                    auto & entry = found->second;
                    switch (item.iSubItem) {

                        case 0:
                            try {
                                // TODO: figure out this without exceptions?
                                this->temporary = database.names.get.query <std::wstring_view> (SQLite::Blob (entry.id));
                                item.pszText = &this->temporary [0];

                            } catch (const SQLite::EmptyResultException &) {
                                // no custom name

                                if (!entry.resolved.empty ()) {
                                    item.pszText = const_cast <wchar_t *> (entry.resolved.c_str ());
                                } else {
                                    item.pszText = const_cast <wchar_t *> (entry.original.c_str ());
                                }
                                // TODO: eid if empty? symbol?
                            }
                            break;

                        case 1:
                            item.pszText = const_cast <wchar_t *> (entry.original.c_str ());
                            break;
                        case 2:
                            item.pszText = const_cast <wchar_t *> (entry.resolved.c_str ());
                            break;
                        case 3:
                            try {
                                this->temporary = database.names.get.query <std::wstring> (SQLite::Blob (entry.id));
                                item.pszText = &this->temporary [0];
                            } catch (const SQLite::EmptyResultException &) {
                                // keep blank
                            }
                            break;

                        case 4:
                            if (this->table == Node::table::identities) {
                                this->temporary = entry.id.identity.serialize ();
                            } else {
                                this->temporary = entry.id.serialize ();
                            }
                            item.pszText = &this->temporary [0];
                            break;

                        case 5: {
                            auto st = raddi::wintime (entry.id.timestamp);
                            wchar_t szTime [256];
                            wchar_t szDate [256];

                            GetTimeFormat (LOCALE_USER_DEFAULT, 0, &st, NULL, szTime, 256);
                            GetDateFormat (LOCALE_USER_DEFAULT, 0, &st, NULL, szDate, 256);

                            this->temporary = szDate;
                            this->temporary += L" ";
                            this->temporary += szTime;

                            item.pszText = &this->temporary [0];
                        } break;

                        case 6:
                            this->temporary = std::to_wstring (entry.threads);
                            item.pszText = &this->temporary [0];
                            break;
                        case 7:
                            this->temporary = std::to_wstring (entry.participants);
                            item.pszText = &this->temporary [0];
                            break;
                        case 8:
                            this->temporary = std::to_wstring (entry.posts_by_author);
                            item.pszText = &this->temporary [0];
                            break;
                    }
                }
            }
            break;

        case LVN_ODFINDITEM:
            if (auto find = reinterpret_cast <NMLVFINDITEM *> (nm)) {
                // CreateSearchBox (GetParent (this->hWnd), ..., find.wChar);
                // TODO: display floating searchbox
            }
            return -1; // not found

        case LVN_BEGINLABELEDIT:
            if (auto hEdit = ListView_GetEditControl (nm->hwndFrom)) {
                if (design.may_need_fix_alpha) {
                    SetWindowSubclass (hEdit, AlphaSubclassProcedure, 0, 0);
                }
            }
            return FALSE;

        case LVN_ENDLABELEDIT:
            if (auto edited = reinterpret_cast <NMLVDISPINFO *> (nm)) {
                if (edited->item.pszText) { // not cancelled
                    database.names.set (SQLite::Blob (this->cache.at (edited->item.iItem).id), edited->item.pszText);
                    this->parent->FinishCommandInAllWindows (Window::FinishCommand::RefreshList);
                    return TRUE;
                }
            }
            break;

        // case : // header dropped?
            //ListView_GetColumnOrderArray

        case LVN_COLUMNCLICK: // set sorting
            break;
    }
    return 0;
}

LRESULT ListOfChannels::OnContextMenu (const Window * parent, LONG x, LONG y) {
    int id;
    switch (ListView_OnContextMenu (parent, this->hWnd, x, y, &id)) {
        case ListPart::Item:
            TrackContextMenu (parent->hWnd, x, y, Menu::ListChannels, true);
            break;
        case ListPart::Group:
        case ListPart::Canvas:
            TrackContextMenu (parent->hWnd, x, y, Menu::ListChannels, false);
            break;
    }
    return 0;
}

void ListOfChannels::prefetch (int row) {
    immutability guard (connection.lock);
    this->unsynchronized_prefetch (row);
}

template <Node::table Table>
void ListOfChannels::unsynchronized_prefetch_original_name_by_index (int row) {
    using meta = DbTableMeta <Table>;
    auto table = meta::table ();
    std::size_t size = 0;

    struct : public meta::layout {
        std::uint8_t description [256]; // must accomodate 'skip' + max_channel_name_size or max_identity_name_size, TODO: static_assert
    } data;

    std::uint64_t index = 0;
    if (meta::reversed) {
        index = table->size () - row - 1;
    } else {
        index = row;
    }

    if (table->get (index, raddi::db::read::identification_and_content, &data, &size)) {

        std::size_t proof_size = 0;
        if (data.proof (size, &proof_size)) {

            auto skip = sizeof (meta::layout) - sizeof (raddi::entry);
            auto text = data.content () + skip;
            auto length = raddi::content::is_plain_line (text, size - proof_size - skip);

            CacheEntry entry;
            entry.id = data.id;
            entry.original = u82ws (text, length);

            this->cache.insert_or_assign (row, entry);
        }
    }
}

void ListOfChannels::unsynchronized_prefetch (int row) {
    if (sort || filter) {



    } else {
        switch (this->table) {
            case Node::table::identities:
                this->unsynchronized_prefetch_original_name_by_index <Node::table::identities> (row);
                break;
            case Node::table::channels:
                this->unsynchronized_prefetch_original_name_by_index <Node::table::channels> (row);
                 break;
        }
    }
}
