#include "menus.h"

HMENU hMainTabsMenu = NULL;
HMENU hListTabsMenu = NULL;
// HMENU hListHeadMenu = NULL;
// HMENU hListItemMenu = NULL;

void InitializeMenus (HINSTANCE hInstance) {
    // TODO: consider preloading only some and those rarely used load on demand

    hMainTabsMenu = GetSubMenu (LoadMenu (hInstance, MAKEINTRESOURCE (0x10)), 0);
    hListTabsMenu = GetSubMenu (LoadMenu (hInstance, MAKEINTRESOURCE (0x20)), 0);
    // hListHeadMenu = GetSubMenu (LoadMenu (hInstance, MAKEINTRESOURCE (0x21)), 0);
    // hListItemMenu = GetSubMenu (LoadMenu (hInstance, MAKEINTRESOURCE (0x22)), 0);
}
