#include "menus.h"
#include "window.h"
#include "data.h"
#include <cstddef>

namespace {
    HMENU menus [Menu::MenuCount] = {};
    HMENU menuListsGroups = NULL;

    std::vector <std::pair <int, int>> mappingListsGroups;

    void PreLoadMenu (HINSTANCE hInstance, Menu::ID m) {
        menus [m] = GetSubMenu (LoadMenu (hInstance, MAKEINTRESOURCE (m)), 0);
    }

    template <typename F>
    void EnableOrDisableEachMenuString (HMENU menu, F lambda) {
        auto n = GetMenuItemCount (menu);
        for (auto i = 0; i < n; ++i) {

            wchar_t prefix [2];
            if (GetMenuString (menu, i, prefix, 2, MF_BYPOSITION)) {
                EnableMenuItem (menu, i, MF_BYPOSITION | (lambda (GetMenuItemID (menu, i), prefix [0]) ? MF_ENABLED : MF_GRAYED));
            }
        }
    }
}

Menu::ID Menu::LastTracked = (Menu::ID) 0;

void InitializeMenus (HINSTANCE hInstance) {
    using namespace Menu;

    // TODO: consider preloading only some and those rarely used load on demand

    for (auto m = 1u; m != sizeof menus / sizeof menus [0]; ++m) {
        if (auto menu = LoadMenu (hInstance, MAKEINTRESOURCE (m))) {
            menus [m] = GetSubMenu (menu, 0);
        }
    }

    SetMenuDefaultItem (menus [ListItem], 0, MF_BYPOSITION);
    SetMenuDefaultItem (menus [ListGroup], 0, MF_BYPOSITION);
    SetMenuDefaultItem (menus [ListChannels], 0, MF_BYPOSITION);

    menuListsGroups = CreatePopupMenu ();
}

void UpdateContextMenu (Menu::ID m, wchar_t code, UINT base, const std::vector <std::wstring> & data) {
    auto n = GetMenuItemCount (menus [m]);
    for (auto i = 0; i < n; ++i) {

        wchar_t prefix [4] = { 0,0,0,0 };
        if (GetMenuString (menus [m], i, prefix, 4, MF_BYPOSITION)) {
            if (prefix [0] == code || prefix [1] == code || prefix [2] == code) {

                if (auto popup = GetSubMenu (menus [m], i)) {

                    // remove all entries except those marked by code, or separators

                    int j = 0;
                    bool top = false;
                    while (GetMenuString (popup, j, prefix, 2, MF_BYPOSITION)) {
                        if (prefix [0] != code && prefix [0] != L'\0') {
                            DeleteMenu (popup, j, MF_BYPOSITION);
                            if (j == 0) {
                                top = true;
                            }
                        } else {
                            ++j;
                        }
                    }

                    // insert entries from map

                    j = 0;
                    for (auto & text : data) {
                        InsertMenu (popup, top ? j : -1, MF_BYPOSITION, base + j, text.c_str ());
                        ++j;
                    }
                }
            }
        }
    }
}

void EnableDisableContextItem (Menu::ID m, wchar_t code, UINT base, int index, UINT flags) {
    auto n = GetMenuItemCount (menus [m]);
    for (auto i = 0; i < n; ++i) {

        wchar_t prefix [4] = { 0,0,0,0 };
        if (GetMenuString (menus [m], i, prefix, 4, MF_BYPOSITION)) {
            if (prefix [0] == code || prefix [1] == code || prefix [2] == code) {

                if (auto popup = GetSubMenu (menus [m], i)) {
                    EnableMenuItem (popup, index, MF_BYPOSITION | flags);
                }
            }
        }
    }
}

void EnableContextItem (Menu::ID m, wchar_t code, UINT base, int index) {
    EnableDisableContextItem (m, code, base, index, MF_ENABLED);
}
void DisableContextItem (Menu::ID m, wchar_t code, UINT base, int index) {
    EnableDisableContextItem (m, code, base, index, MF_GRAYED);
}

void ReplacePopupMenu (Menu::ID m, wchar_t code, HMENU replace) {
    auto n = GetMenuItemCount (menus [m]);
    for (auto i = 0; i < n; ++i) {

        wchar_t string [256];
        if (GetMenuString (menus [m], i, string, 256, MF_BYPOSITION)) {
            if (string [0] == code || string [1] == code || string [2] == code) {

                ModifyMenu (menus [m], i, MF_BYPOSITION | MF_POPUP, (UINT_PTR) replace, string);
            }
        }
    }
}

void UpdateListsSubMenu () {
    mappingListsGroups.clear ();
    while (DeleteMenu (menuListsGroups, 0, MF_BYPOSITION))
        ;

    auto id = 0;
    std::map <int, HMENU> lists;

    while (database.lists.groups.query.next ()) {
        auto group = database.lists.groups.query.get <int> (0);
        auto list = database.lists.groups.query.get <int> (1);
        auto text = database.lists.groups.query.get <std::wstring> (2);

        if (!lists.count (list)) {
            lists [list] = CreatePopupMenu ();
        }

        if (AppendMenu (lists [list], 0, Window::ID::LIST_SUBMENU_BASE + id, text.c_str ())) {
            mappingListsGroups.push_back ({ list, group });
            ++id;
        }
    }

    while (database.lists.query.next ()) {
        auto list = database.lists.query.get <int> (0);
        auto text = database.lists.query.get <std::wstring> (1);

        if (lists.count (list)) {
            //if (GetMenuItemCount (lists [list]) > 1) {
                AppendMenu (menuListsGroups, MF_POPUP, (UINT_PTR) lists [list], text.c_str ());
                AppendMenu (lists [list], MF_SEPARATOR, 0, NULL);
                if (AppendMenu (lists [list], MF_STRING, Window::ID::LIST_SUBMENU_BASE + id, LoadString (0x71).c_str ())) {
                    mappingListsGroups.push_back ({ list, -1 });
                    ++id;
                }
            /*} else {
                AppendMenu (menuListsGroups, 0, GetMenuItemID (lists [list], 0), text.c_str ());
                DestroyMenu (lists [list]);
            }*/
        }
    }

    if (!lists.empty ()) {
        AppendMenu (menuListsGroups, MF_SEPARATOR, 0, NULL);
    }
    if (AppendMenu (menuListsGroups, MF_STRING, Window::ID::LIST_SUBMENU_BASE + id, LoadString (0x68).c_str ())) {
        mappingListsGroups.push_back ({ -1, -1 });
    }
}

bool ResolveListsSubMenuItem (UINT id, int * list, int * group) {
    id -= Window::ID::LIST_SUBMENU_BASE;
    if (id < mappingListsGroups.size ()) {
        
        if (list) {
            *list = mappingListsGroups [id].first;
        }
        if (group) {
            *group = mappingListsGroups [id].second;
        }
        return true;
    } else
        return false;
}

void TrackContextMenu (HWND hWnd, LONG x, LONG y, Menu::ID m, std::intptr_t item) {
    if (menus [m]) {
        switch (m) {
            using namespace Menu;

            case ViewTabs: // TODO: some items may be enabled only for stacks, like "Unstack tabs"
            case ListTabs:
            case ListGroup:
                EnableOrDisableEachMenuString (menus [m],
                                               [item] (auto id, auto prefix) {
                                                   return (item > 0) || (prefix == L'\x200C');
                                               });
                break;

            case ListItem: // ??
                UpdateListsSubMenu ();
                ReplacePopupMenu (m, L'\x200B', menuListsGroups);
                break;

            case ListChannels:
                if (item > 0) {
                    UpdateListsSubMenu ();
                    ReplacePopupMenu (m, L'\x200B', menuListsGroups);
                }
                EnableOrDisableEachMenuString (menus [m],
                                               [item](auto id, auto prefix) {
                                                   return (item > 0) || (prefix == L'\x200C');
                                               });
                break;
        }

        Menu::LastTracked = m;
        TrackPopupMenu (menus [m], TPM_RIGHTBUTTON, x, y, 0, hWnd, NULL);
    }
}
