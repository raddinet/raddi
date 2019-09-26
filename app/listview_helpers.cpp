#include "listview_helpers.h"
#include <CommCtrl.h>

#include <VersionHelpers.h>
#include <set>

int ListView_GetFocusedGroupId (HWND hList, std::wstring * title, RECT * r) {
    auto group = ListView_GetFocusedGroup (hList);
    if (group != -1) {
        wchar_t buffer [256]; // enough?

        LVGROUP lvGroup;
        lvGroup.cbSize = sizeof lvGroup;
        lvGroup.mask = LVGF_HEADER | LVGF_GROUPID;
        lvGroup.pszHeader = buffer;
        lvGroup.cchHeader = sizeof buffer / sizeof buffer [0];
        lvGroup.pszHeader [0] = 0;

        if (ListView_GetGroupInfoByIndex (hList, group, &lvGroup) != -1) {

            if (title) {
                *title = lvGroup.pszHeader;
            }
            if (r) {
                ListView_GetGroupRect (hList, lvGroup.iGroupId, LVGGR_HEADER, r);
            }
            return lvGroup.iGroupId;
        }
    }
    return 0;
}

std::vector <std::wstring> ListView_GetGroupNames (HWND hList) {
    wchar_t buffer [256];
    
    LVGROUP lvGroup;
    lvGroup.cbSize = sizeof lvGroup;
    lvGroup.mask = LVGF_HEADER;
    lvGroup.pszHeader = buffer;
    lvGroup.cchHeader = sizeof buffer / sizeof buffer [0];
    lvGroup.pszHeader [0] = 0;

    auto n = ListView_GetGroupCount (hList);

    std::vector <std::wstring> groups;
    if (n) {
        groups.reserve (n);

        for (auto i = 0; i != n; ++i) {
            if (ListView_GetGroupInfoByIndex (hList, i, &lvGroup)) {
                groups.push_back (lvGroup.pszHeader);
            }
        }
    }
    return groups;
}

int ListView_MoveItemsToGroup (HWND hList, int group, const std::vector <int> & items) {
    int n = 0;
    for (const auto index : items) {
        LVITEM item;
        item.mask = LVIF_GROUPID;
        item.iItem = index;
        item.iSubItem = 0;

        if (ListView_GetItem (hList, &item)) {
            item.iGroupId = group;
            if (ListView_SetItem (hList, &item)) {
                ++n;
            }
        }
    }
    return n;
}

LPARAM ListView_GetItemParam (HWND hListView, int index) {
    LVITEM item;
    item.mask = LVIF_PARAM;
    item.iItem = index;
    item.iSubItem = 0;

    if (ListView_GetItem (hListView, &item)) {
        return item.lParam;
    } else
        return 0;
}

int ListView_GetItemGroupId (HWND hListView, int index) {
    LVITEM item;
    item.mask = LVIF_GROUPID;
    item.iItem = index;
    item.iSubItem = 0;

    if (ListView_GetItem (hListView, &item)) {
        return item.iGroupId;
    } else
        return 0;
}

int ListView_SetGroupTitle (HWND hListView, int groupID, const std::wstring & text) {
    LVGROUP lvGroup;
    lvGroup.cbSize = sizeof lvGroup;
    lvGroup.mask = LVGF_HEADER;
    lvGroup.pszHeader = const_cast <wchar_t *> (&text [0]);
    lvGroup.cchHeader = text.length ();

    return (int) ListView_SetGroupInfo (hListView, groupID, &lvGroup);
}

int ListView_DeleteGroupAndItems (HWND hListView, int groupID) {
    auto deleted = 0;
    if (auto n = ListView_GetItemCount (hListView)) {
        LVITEM item;
        item.mask = LVIF_GROUPID;
        item.iItem = n;
        item.iSubItem = 0;

        while (--item.iItem >= 0) {
            if (ListView_GetItem (hListView, &item)) {
                if (item.iGroupId == groupID) {
                    if (ListView_DeleteItem (hListView, item.iItem)) {
                        ++deleted;
                    }
                }
            }
        }
    }

    ListView_RemoveGroup (hListView, groupID);
    return deleted;
}

int ListView_DeleteEmptyGroups (HWND hListView) {
    auto removed = 0;
    if (auto n = ListView_GetGroupCount (hListView)) {
        LVGROUP group;
        group.cbSize = sizeof group;
        group.mask = LVGF_GROUPID | LVGF_ITEMS;

        while (--n >= 0) {
            if (ListView_GetGroupInfoByIndex (hListView, n, &group)) {
                if (group.cItems == 0) {
                    if (ListView_RemoveGroup (hListView, group.iGroupId) != -1) {
                        ++removed;
                    }
                }
            }
        }
    }
    return removed;
}

int ListView_GetGroupId (HWND hListView, int groupIndex) {
    LVGROUP group;
    group.cbSize = sizeof group;
    group.mask = LVGF_GROUPID;

    if (ListView_GetGroupInfoByIndex (hListView, groupIndex, &group)) {
        return group.iGroupId;
    } else
        return 0;

}

int ListView_GetGroupIdIndex (HWND hListView, int groupID) {
    auto i = 0;
    auto n = ListView_GetGroupCount (hListView);
    for (; i != n; ++i) {
        LVGROUP group;
        group.cbSize = sizeof group;
        group.mask = LVGF_GROUPID;

        if (ListView_GetGroupInfoByIndex (hListView, i, &group)) {
            if (group.iGroupId == groupID)
                return i;
        }
    }
    return -1;
}

int ListView_CopyGroupToListView (HWND hListView, int groupID, HWND hTargetListView) {
    auto n = 0;
    wchar_t buffer [256];

    if (auto i = ListView_GetItemCount (hListView)) {
        LVITEM item;
        item.mask = LVIF_GROUPID | LVIF_PARAM | LVIF_STATE | LVIF_TEXT | LVIF_IMAGE | LVIF_INDENT;
        item.iSubItem = 0;

        while (--i >= 0) {
            item.iItem = i;
            item.pszText = buffer;
            item.cchTextMax = sizeof buffer / sizeof buffer [0];

            if (ListView_GetItem (hListView, &item)) {
                if (item.iGroupId == groupID) {
                    if (ListView_InsertItem (hTargetListView, &item) >= 0)
                        ++n;
                }
            }
        }
    }
    return n;
}
