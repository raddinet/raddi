#ifndef TABS_H
#define TABS_H

#include <windows.h>
#include <commctrl.h>

// quick and dirty TabControl for use in window caption

// TODO: scroll buttons, bring tab to visibility

ATOM InitializeTabControl (HINSTANCE);
HWND CreateTabControl (HINSTANCE, HWND, UINT style, UINT id = 0);

// WM_USER+3
//  - returns: minimal advised height of the control

// TCM_INSERTITEM
//  - wParam: some ID of the item
//  - lParam: string (if starts with \x0001 then without close button)
//  - returns: number of tabs

// TCM_GETITEM
//  - wParam: tab index
//  - returns: tab ID

// TCM_GETITEMRECT - as documented in MSDN
// TCM_GETITEMCOUNT - as documented in MSDN
// TCM_DELETEALLITEMS - as documented in MSDN

// TCM_GETCURSEL
//  - lParam: optional pointer to LONG_PTR to receive current tab's ID
//  - returns: currently active tab index


// TODO:

// TCM_DELETEITEM
//  - wParam: tab ID to delete
//  - returns true/false (false if no such ID exists)

// TCM_SETCURSEL
//  - wParam: tab ID to activate
//  - returns: tab index, or -1 if no such ID


#endif
