#ifndef TABS_H
#define TABS_H

#include <windows.h>
#include <string>
#include <vector>
#include <map>

ATOM InitializeTabControl (HINSTANCE);
HWND CreateTabControl (HINSTANCE, HWND, UINT style, UINT id = 0);

struct Tab {
    std::wstring    text;
    HWND            content = NULL;
    HFONT           font = NULL;
    HICON           icon = NULL;
    std::uint16_t   min_width = 0;
    std::uint16_t   max_width = 0;
    std::uint16_t   badge = 0;    // 0 = not rendered
    std::uint8_t    progress = 0; // 0 = off, 1 = 0%, 255 = 100%
    bool            close = true;
	bool			fit = false; // fit width to text
	bool			locked = false; // width locked to fit
};
struct TabControlInterface {
    std::map <std::size_t, Tab> tabs; // IDs are application-defined
    POINT                       dpi;
    std::uint16_t				min_tab_width = 0;
    std::uint16_t				max_tab_width = 0;
	bool						stacking = false; // allow user to stack tabs
	bool						badges = false; // add padding for tab badges

    enum class VisualStyle {
        Native,
        Light,
        Dark
    } style = VisualStyle::Native;

    // computed:
    SIZE                        minimum;
    std::vector <std::size_t>   overflow; // tabs that did not fit

public:
    virtual void update () = 0; // call update when 'tabs' change
    virtual void stack (std::size_t which, std::size_t onto, bool after) = 0; // move 'which' tab into stack of 'onto' either right after or as last tab
};

static inline TabControlInterface * TabControl (HWND hControl) {
    return reinterpret_cast <TabControlInterface *> (GetWindowLongPtr (hControl, GWLP_USERDATA));
}
static inline TabControlInterface * TabControl (HWND hParent, UINT id) {
    return TabControl (GetDlgItem (hParent, id));
}

#endif
