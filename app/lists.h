#ifndef LISTS_H
#define LISTS_H

#include <windows.h>

#include <string>
#include <vector>

class Window;
struct WindowEnvironment;
struct TabControlInterface;

enum class ListPart {
    Canvas,
    Group,
    Item,
};

// ListView_OnContextMenu
//  - find proper menu anchor point whether activated by mouse or from keyboard
//  - returns where the click happened and ID (item or group)
//
ListPart ListView_OnContextMenu (const WindowEnvironment * parent, HWND hListView, LONG & x, LONG & y, int * id);

// ListView_CustomHeaderSubclassProcedure
//  - draws custom, simpler, dark mode enabled, header drawing callbacks out from listview message processing
//  - install with SetWindowSubclass (hListView, ..., 0, (DWORD_PTR) (const Window *) parent);
//
LRESULT CALLBACK ListView_CustomHeaderSubclassProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam,
                                                         UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

// User-defined list of channels are not represented by an object, just HWND with IDs LIST_BASE...LIST_LAST

namespace Lists {
    bool Load (const Window * parent, TabControlInterface * tc);
    HWND Create (const Window * parent, TabControlInterface * tc, std::intptr_t id, const std::wstring & text);
    void OnDpiChange (TabControlInterface * tc, long dpi);
    
    LRESULT OnNotify (const Window * parent, NMHDR *);
    LRESULT OnContextMenu (const Window * parent, HWND, int, LONG x, LONG y);

    int CreateGroup (HWND, std::intptr_t id, const std::wstring &);
    int DeleteGroup (HWND, std::intptr_t id);
    int CleanGroups (HWND);

    int InsertEntry (HWND, std::intptr_t id, int group);

    namespace Internal {
        HWND Create (const Window * parent, std::intptr_t id, const std::vector <std::wstring> & columns);
        void CreateFinalize (HWND hList);
        std::vector <std::wstring> GetColumns ();
    }
}

#endif
