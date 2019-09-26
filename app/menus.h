#ifndef MENUS_H
#define MENUS_H

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Menu {
    enum ID : std::uint16_t { 
        ViewTabs = 0x10,
        ListTabs = 0x20,

        ListGroup = 0x22,
        ListItem = 0x23,
        List = 0x24,
        ListChannels = 0x25,

        MenuCount,
    };

    extern ID LastTracked;
}

void InitializeMenus (HINSTANCE hInstance);

void UpdateContextMenu (Menu::ID m, wchar_t code, UINT base, const std::vector <std::wstring> & data);
void EnableContextItem (Menu::ID m, wchar_t code, UINT base, int index);
void DisableContextItem (Menu::ID m, wchar_t code, UINT base, int index);

void UpdateListsSubMenu ();
bool ResolveListsSubMenuItem (UINT id, std::intptr_t * list, std::intptr_t * group);

void TrackContextMenu (HWND hWnd, LONG x, LONG y, Menu::ID m, std::intptr_t parameter);

#endif
