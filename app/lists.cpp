#include "lists.h"
#include "listview_helpers.h"
#include "../common/node.h"
#include "data.h"
#include "resolver.h"
#include "window.h"
#include "menus.h"
#include "../common/log.h"

#include <VersionHelpers.h>
#include <set>

#pragma warning (disable:26454) // -1u warnings

extern "C" IMAGE_DOS_HEADER __ImageBase;
extern Data database;
extern Resolver resolver;
extern Design design;

ListPart ListView_OnContextMenu (const WindowEnvironment * parent, HWND hListView, LONG & x, LONG & y, int * id) {
    *id = 0;

    if ((x == -1) && (y == -1)) {
        RECT r;

        auto item = ListView_GetNextItem (hListView, -1, LVNI_FOCUSED);
        if (item != -1) {

            if (ListView_GetItemRect (hListView, item, &r, LVIR_LABEL)) {
                MapWindowPoints (hListView, NULL, reinterpret_cast <POINT *> (&r), 2);

                x = r.left + parent->metrics [SM_CXSMICON] / 2;
                y = r.bottom - parent->metrics [SM_CYSMICON] / 4;

                *id = item;
                return ListPart::Item;
            }
        } else {
            auto group = ListView_GetFocusedGroup (hListView);
            if (group != -1) {

                LVGROUP lvGroup;
                lvGroup.cbSize = sizeof lvGroup;
                lvGroup.mask = LVGF_GROUPID;

                if (ListView_GetGroupInfoByIndex (hListView, group, &lvGroup)) {
                    if (ListView_GetGroupRect (hListView, lvGroup.iGroupId, LVGGR_LABEL, &r)) {
                        MapWindowPoints (hListView, NULL, reinterpret_cast <POINT *> (&r), 2);

                        x = r.left;
                        y = r.bottom;

                        *id = lvGroup.iGroupId;
                        return ListPart::Group;
                    }
                }
            }
        }
    } else {
        LVHITTESTINFO info = { { x, y }, 0, 0, 0 };
        MapWindowPoints (NULL, hListView, &info.pt, 1);

        if (SendMessage (hListView, LVM_HITTEST, (winver >= 6) ? -1 : 0, reinterpret_cast <LPARAM> (&info)) != -1) {
            *id = info.iItem;

            if (info.flags & LVHT_EX_GROUP) return ListPart::Group;
            if (info.flags & LVHT_ONITEM) return ListPart::Item;
        }
    }
    return ListPart::Canvas;
}

bool Header_GetItemText (HWND hHeader, int index, wchar_t * buffer, std::size_t length) {
    HDITEM hdi;
    hdi.mask = HDI_TEXT;
    hdi.pszText = buffer;
    if (length <= MAXINT) {
        hdi.cchTextMax = (int) length;
    } else {
        hdi.cchTextMax = MAXINT;
    }
    return Header_GetItem (hHeader, index, &hdi);
}

LRESULT CALLBACK ListView_CustomHeaderSubclassProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                         UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    /*if (uIdSubclass) {
        raddi::log::event (0xA1F0, "ListView_CustomHeaderSubclassProcedure", hWnd, uIdSubclass, dwRefData);
    }*/
    switch (message) {
        case WM_NOTIFY:

            if (auto nm = reinterpret_cast <NMHDR *> (lParam))
                switch (nm->code) {

                    case NM_CUSTOMDRAW:
                        if (auto draw = reinterpret_cast <NMCUSTOMDRAW *> (nm))
                            switch (draw->dwDrawStage) {

                                case CDDS_PREPAINT:
                                    return CDRF_NOTIFYITEMDRAW;

                                case CDDS_ITEMPREPAINT:
                                    auto window = reinterpret_cast <const Window *> (dwRefData);
                                    auto r = draw->rc;

                                    r.top = r.bottom - 1;
                                    r.left += 3 * window->dpi / 96;
                                    r.right -= 4 * window->dpi / 96;

                                    if (!ListView_IsGroupViewEnabled (hWnd)) {
                                        if (r.left < r.right) {
                                            if (design.light) {
                                                FillRect (draw->hdc, &r, GetSysColorBrush (COLOR_3DSHADOW));
                                            } else {
                                                FillRect (draw->hdc, &r, GetSysColorBrush (COLOR_3DDKSHADOW));
                                            }
                                        }
                                    }
                                    if (design.light) {
                                        SetTextColor (draw->hdc, GetSysColor (COLOR_WINDOWTEXT));
                                    } else {
                                        SetTextColor (draw->hdc, 0xFFFFFF);
                                    }

                                    SetBkMode (draw->hdc, TRANSPARENT);

                                    wchar_t text [256];
                                    Header_GetItemText (nm->hwndFrom, (int) draw->dwItemSpec, text, 256);

                                    DrawCompositedTextOptions opts;
                                    opts.theme = GetWindowTheme (hWnd);
                                    opts.font = window->fonts.text.handle;

                                    r.top = draw->rc.top;
                                    DrawCompositedText (draw->hdc, text, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, r, &opts);
                                    return CDRF_SKIPDEFAULT;// */
                            }
                        break;
                }
            break;
    }
    return DefSubclassProc (hWnd, message, wParam, lParam);
}

bool Lists::Load (const Window * parent, TabControlInterface * tc) {
    try {
        auto columns = Lists::Internal::GetColumns ();

        while (database.lists.query.next ()) {
            auto id = database.lists.query.get <int> (0);

            if (auto h = Lists::Internal::Create (parent, id, columns)) {
                tc->tabs [id].text = database.lists.query.get <std::wstring> (1);
                tc->tabs [id].content = h;
                tc->tabs [id].close = false;
                tc->update ();
            } else
                return false;
        }

        std::map <int, HWND> mapListGroup;

        while (database.lists.groups.query.next ()) {
            auto list = database.lists.groups.query.get <int> (1);

            auto itab = tc->tabs.find (list);
            if (itab != tc->tabs.end ()) {
                auto group = database.lists.groups.query.get <int> (0);
                auto text = database.lists.groups.query.get <std::wstring> (2);

                if (Lists::CreateGroup (itab->second.content, group, text) != -1) {
                    mapListGroup [group] = itab->second.content;
                }
            }
        }

        std::wstring text;
        while (database.lists.data.query.next ()) {
            auto id = database.lists.data.query.get <int> (0);
            auto group = database.lists.data.query.get <int> (1);
            auto hListView = mapListGroup [group];

            try {
                resolver.add (hListView, database.lists.data.query.get <SQLite::Blob> (2));

                Lists::InsertEntry (hListView, id, group);
            } catch (const SQLite::BadBlobAssignmentException &) {
                // TODO: report
                // and delete invalid item
                database.lists.data.remove (id);
            }
        }

        Lists::OnDpiChange (tc, parent->dpi);

        for (const auto & tab : tc->tabs) {
            Lists::Internal::CreateFinalize (tab.second.content);
        }
        return true;

    } catch (const std::bad_alloc &) {
        ReportOutOfMemory (parent->hWnd, 0);
    }
    return false;
}

HWND Lists::Create (const Window * parent, TabControlInterface * tc, std::intptr_t id, const std::wstring & text) {
    if (auto h = Lists::Internal::Create (parent, id, Lists::Internal::GetColumns ())) {
        tc->tabs [id].text = text;
        tc->tabs [id].content = h;
        tc->tabs [id].close = false;
        tc->update ();

        Lists::Internal::CreateFinalize (h);
        return h;
    } else
        return NULL;
}

HWND Lists::Internal::Create (const Window * parent, std::intptr_t id, const std::vector <std::wstring> & columns) {
    static constexpr auto style = WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | LVS_REPORT | LVS_EDITLABELS | LVS_SHAREIMAGELISTS;// | LVS_NOCOLUMNHEADER;
    static constexpr auto extra = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;

    if (auto h = CreateWindowEx (WS_EX_NOPARENTNOTIFY, WC_LISTVIEW, L"", style,
                                 0,0,0,0, parent->hWnd, (HMENU) (Window::ID::LIST_BASE + id), NULL, NULL)) {

        ListView_SetToolTips (h, parent->hToolTip);
        ListView_SetExtendedListViewStyle (h, extra);

        if (winver >= 6) {
            SetWindowSubclass (h, ListView_CustomHeaderSubclassProcedure, 1, (DWORD_PTR) parent);
        }

        SendMessage (h, WM_SETREDRAW, FALSE, 0);
        SetWindowLongPtr (h, GWLP_USERDATA, (LONG_PTR) id);

        LVCOLUMN column;
        column.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;// | LVCF_ORDER;
        column.cx = 0;
        column.fmt = LVCFMT_LEFT;
        column.iSubItem = 0;

        for (const auto & label : columns) {
            column.pszText = const_cast <wchar_t *> (label.c_str ());
            ListView_InsertColumn (h, column.iSubItem++, &column);
        }
        
        return h;
    } else
        return NULL;
}

void Lists::Internal::CreateFinalize (HWND h) {
    if (ListView_GetGroupCount (h) > 1) {
        ListView_EnableGroupView (h, TRUE);
    }
    SendMessage (h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect (h, NULL, TRUE);
}

std::vector <std::wstring> Lists::Internal::GetColumns () {
    std::vector <std::wstring> columns;
    columns.reserve (16);

    std::wstring text;
    for (auto i = 0u; (text = LoadString (0x50 + i)), !text.empty (); ++i) {
        columns.push_back (text);
    }
    return columns;
}

void Lists::OnDpiChange (TabControlInterface * tc, long dpi) {
    LVGROUPMETRICS metrics;
    metrics.cbSize = sizeof metrics;
    metrics.mask = LVGMF_BORDERSIZE;
    metrics.Top = 0;
    metrics.Left = 0;
    metrics.Right = 0;
    metrics.Bottom = 8 * dpi / 96;

    for (const auto & [id, tab] : tc->tabs) {
        if (id > 0) {
            ListView_SetColumnWidth (tab.content, 0, 128 * dpi / 96); // TODO: keep user preferences?
            ListView_SetColumnWidth (tab.content, 4, 172 * dpi / 96);
            ListView_SetColumnWidth (tab.content, 5, 128 * dpi / 96);
            ListView_SetGroupMetrics (tab.content, &metrics);
        }
    }
}

LRESULT Lists::OnNotify (const Window * parent, NMHDR * nm) {
    static std::wstring temporary;
    static class {
        HWND control = NULL;
        LPARAM item = 0;
    public:
        raddi::eid eid;
        std::wstring idstring;
        std::wstring custom;
        std::wstring original;
        std::wstring resolved;

        struct {
            bool custom = false;
            bool original = false;
            bool resolved = false;
        } valid;

        void update (NMHDR * nm, LPARAM item) {
            if (this->control != nm->hwndFrom || this->item != item) {
                this->control = nm->hwndFrom;
                this->item = item;

                this->eid = database.lists.data.get.query <SQLite::Blob> (item);
                if (this->eid.timestamp == this->eid.identity.timestamp) {
                    this->idstring = this->eid.identity.serialize ();
                } else {
                    this->idstring = this->eid.serialize ();
                }
                try {
                    this->custom = database.names.getByListedId.query <std::wstring_view> (item);
                    this->valid.custom = true;
                } catch (const SQLite::EmptyResultException &) {
                    this->valid.custom = false;
                }
                this->valid.resolved = resolver.get (this->eid, nm->hwndFrom, &this->resolved);
                this->valid.original = resolver.get_original_title (this->eid, &this->original);
        }
        }
    } cache;

    switch (nm->code) {
        case LVN_BEGINLABELEDIT:
            if (auto hEdit = ListView_GetEditControl (nm->hwndFrom)) {
                if (design.may_need_fix_alpha) {
                    SetWindowSubclass (hEdit, AlphaSubclassProcedure, 0, 0);
                }
            }
            return FALSE;

        case LVN_GETDISPINFO:
            if (auto info = reinterpret_cast <NMLVDISPINFO *> (nm)) {
                cache.update (nm, info->item.lParam);

                switch (info->item.iSubItem) {
                    case 0:
                        if (cache.valid.custom) {
                            info->item.pszText = &cache.custom [0];
                        } else
                            if (cache.valid.resolved) {
                                info->item.pszText = &cache.resolved [0];
                            } else
                                if (cache.valid.original) {
                                    info->item.pszText = &cache.original [0];
                                } else {
                                    info->item.pszText = &cache.idstring [0];
                                }
                        break;
                    case 1:
                        if (cache.valid.original) {
                            info->item.pszText = &cache.original [0];
                        }
                        break;
                    case 2:
                        if (cache.valid.resolved) {
                            info->item.pszText = &cache.resolved [0];
                        }
                        break;
                    case 3:
                        if (cache.valid.custom) {
                            info->item.pszText = &cache.custom [0];
                        }
                        break;
                    case 4:
                        info->item.pszText = &cache.idstring [0];
                        break;
                    case 5: {
                        auto st = raddi::wintime (cache.eid.timestamp);
                        wchar_t szTime [256];
                        wchar_t szDate [256];

                        GetTimeFormat (LOCALE_USER_DEFAULT, 0, &st, NULL, szTime, 256);
                        GetDateFormat (LOCALE_USER_DEFAULT, 0, &st, NULL, szDate, 256);

                        temporary = szDate;
                        temporary += L" ";
                        temporary += szTime;

                        info->item.pszText = &temporary [0];
                    } break;

                    default:
                        temporary = L"<TODO>";
                        info->item.pszText = &temporary [0];
                        break;
                }
            }
            break;

        case NM_CUSTOMDRAW:
            if (auto draw = reinterpret_cast <NMLVCUSTOMDRAW *> (nm)) {
                switch (draw->nmcd.dwDrawStage) {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT:
                        return CDRF_NOTIFYSUBITEMDRAW;

                    // TODO: colors?

                    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
                        cache.update (nm, draw->nmcd.lItemlParam);
                        if (cache.valid.custom) {
                            switch (draw->iSubItem) {
                                case 1:
                                    SelectObject (draw->nmcd.hdc, parent->fonts.text.handle);
                                    break;
                                case 0:
                                    SelectObject (draw->nmcd.hdc, parent->fonts.italic.handle);
                                    break;
                            }
                            return CDRF_NEWFONT;
                        } else
                            return CDRF_DODEFAULT;
                }
            }
            break;

        case LVN_ITEMACTIVATE:
            if (auto activated = reinterpret_cast <NMITEMACTIVATE *> (nm)) {
                // activated->lParam?
            }
            break;

        case LVN_GETEMPTYMARKUP:
            if (auto markup = reinterpret_cast <NMLVEMPTYMARKUP *> (nm)) {
                markup->dwFlags = EMF_CENTERED;
                if (LoadString ((HINSTANCE) &__ImageBase, 0x24,
                                markup->szMarkup, sizeof markup->szMarkup / sizeof markup->szMarkup [0]))
                    return TRUE;
            }
            break;

        case LVN_ENDLABELEDIT:
            if (auto edited = reinterpret_cast <NMLVDISPINFO *> (nm)) {
                if (edited->item.pszText) { // not cancelled
                    database.names.setByListedId (edited->item.lParam, edited->item.pszText);
                    parent->FinishCommandInAllWindows (Window::FinishCommand::RefreshList);
                    return TRUE;
                }
            }
            break;
    }
    return 0;
}

LRESULT Lists::OnContextMenu (const Window * parent, HWND hList, int listIndex, LONG x, LONG y) {
    int id;
    switch (ListView_OnContextMenu (parent, hList, x, y, &id)) {
        case ListPart::Group:
            DisableContextItem (Menu::ListGroup, L'\x200B', Window::ID::LIST_BASE, listIndex);
            TrackContextMenu (parent->hWnd, x, y, Menu::ListGroup, id);
            EnableContextItem (Menu::ListGroup, L'\x200B', Window::ID::LIST_BASE, listIndex);
            break;
        case ListPart::Item:
            TrackContextMenu (parent->hWnd, x, y, Menu::ListItem, id);
            break;
        case ListPart::Canvas:
            TrackContextMenu (parent->hWnd, x, y, Menu::List, id);
            break;
    }
    return 0;
}

int Lists::CreateGroup (HWND hList, std::intptr_t id, const std::wstring & text) {
    LVGROUP group;
    group.cbSize = sizeof group;
    group.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
    group.state = LVGS_NORMAL; // TODO: support colapsing? add symbols marking colapsed state?
    group.stateMask = group.state;
    group.pszHeader = const_cast <wchar_t *> (text.c_str ());
    group.iGroupId = (int) id;

    return (int) ListView_InsertGroup (hList, -1, &group);
}

int Lists::DeleteGroup (HWND hListView, std::intptr_t id) {
    int n = ListView_DeleteGroupAndItems (hListView, (int) id);
    ListView_EnableGroupView (hListView, ListView_GetGroupCount (hListView) > 1);
    return n;
}

int Lists::CleanGroups (HWND hListView) {
    int n = ListView_DeleteEmptyGroups (hListView);
    ListView_EnableGroupView (hListView, ListView_GetGroupCount (hListView) > 1);
    return n;
}

int Lists::InsertEntry (HWND hList, std::intptr_t id, int group) {
    LVITEM item;
    item.mask = LVIF_TEXT | LVIF_GROUPID | LVIF_PARAM;
    item.iItem = 0;
    item.iSubItem = 0;
    item.pszText = LPSTR_TEXTCALLBACK;
    item.iGroupId = group;
    item.lParam = id;

    return ListView_InsertItem (hList, &item);
}
