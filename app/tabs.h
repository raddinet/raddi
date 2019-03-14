#ifndef TABS_H
#define TABS_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>

ATOM InitializeTabControl (HINSTANCE);
HWND CreateTabControl (HINSTANCE, HWND, UINT style, UINT id = 0);

// TODO: drag&drop with stacking, when enabled
// TODO: show tip only for tabs with shortened texts

struct Tab {
    std::wstring    text;
    std::wstring    tip;
    HWND            content = NULL;
    HFONT           font = NULL;
    HICON           icon = NULL;
    std::uint8_t    progress = 0; // 0 = off, 1 = 0%, 255 = 100%
    bool            close = true;
	bool			fit = false; // fit width to text
	bool			locked = false; // width locked to fit
    unsigned int    badge = 0;    // 0 = not rendered
    
    // computed:
    std::size_t     stack_index = 0;
};

struct TabControlVisualStyle; // TODO
extern TabControlVisualStyle DarkTabControlVisualStyle;
extern TabControlVisualStyle LightTabControlVisualStyle;

struct TabControlInterface {
    std::map <std::intptr_t, Tab> tabs; // IDs are application-defined
    std::uint16_t			dpi;
    std::uint16_t			min_tab_width = 0;
    std::uint16_t			max_tab_width = 0;
	bool					stacking = false; // allow user to stack tabs
	bool					badges = false; // add padding for tab badges
    TabControlVisualStyle * style = nullptr; // NULL - native
    HWND                    hToolTipControl = NULL;

    // computed:
    SIZE                        minimum = { 0, 0 };
    std::vector <std::intptr_t> overflow; // tabs that did not fit

public:
    virtual void update () = 0; // call update when 'tabs' change
    virtual bool request (std::intptr_t tab) = 0; // returns false if no such tab exists
    virtual bool request_stack (std::size_t index) = 0;
    virtual void stack (std::intptr_t which, std::intptr_t onto, bool after) = 0; // move 'which' tab into stack of 'onto' either right after or as last tab
};

static inline TabControlInterface * TabControl (HWND hControl) {
    return reinterpret_cast <TabControlInterface *> (GetWindowLongPtr (hControl, GWLP_USERDATA));
}
static inline TabControlInterface * TabControl (HWND hParent, UINT id) {
    return TabControl (GetDlgItem (hParent, id));
}

#endif
