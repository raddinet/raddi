#ifndef TABS_H
#define TABS_H

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <map>

struct TabControlInterface;

ATOM InitializeTabControl (HINSTANCE);
TabControlInterface * CreateTabControl (HINSTANCE, HWND, UINT style, UINT id = 0);

#define TCN_UPDATED 1

// TODO: drag&drop with stacking, when enabled
// TODO: add support for animated icon (loading)

struct Tab {
    std::wstring    text;
    std::wstring    tip;
    HWND            content = NULL;
    HFONT           font = NULL;
    HICON           icon = NULL;
    unsigned int    badge = 0;    // 0 = not rendered
    std::uint8_t    progress = 0; // 0 = off, 1 = 0%, 255 = 100%
    bool            close = true;
    bool            fit = false; // fit width to text
    
    // computed:
    bool            ellipsis = false;
    std::size_t     stack_index = 0;
};

// struct TabControlVisualStyle; // TODO
// extern TabControlVisualStyle DarkTabControlVisualStyle;
// extern TabControlVisualStyle LightTabControlVisualStyle;

struct TabControlInterface {
    HWND                    hWnd = NULL;
    std::map <std::intptr_t, Tab> tabs; // IDs are application-defined
    std::uint16_t           dpi = 96;
    std::uint16_t           min_tab_width = 0;
    std::uint16_t           max_tab_width = 0;
    std::uint16_t           stacking : 1; // allow user to stack tabs
    std::uint16_t           badges : 1; // add padding for tab badges
    std::uint16_t           dark : 1; // custom dark theme
    // TabControlVisualStyle * style = nullptr; // NULL - native
    struct {
        struct {
            COLORREF        tab = 0x000000; // tab color
            COLORREF        current = 0x101112; // currently selected tab color
            COLORREF        hot = 0x101112; // hot tab color
            COLORREF        inactive = 0x101112;
        } dark;
    } style;
    struct {
        COLORREF            color = 0xFFFFFF;
        COLORREF            hot = 0xFFFFFF;
        COLORREF            down = 0x7F7F7F;
        UINT                glow = 0;
    } buttons;
    HWND                    hToolTipControl = NULL;
    std::intptr_t           contextual = 0; // ID of last right-clicked tab

    // computed:
    std::intptr_t               current = 0;
    SIZE                        minimum = { 0, 0 };
    LONG                        width = 0;
    std::vector <std::intptr_t> overflow; // tabs that did not fit

    static constexpr auto TCN_MCLICK = TCN_FIRST + 1;
    static constexpr auto TCN_MDBLCLK = TCN_FIRST + 1;

public:
    virtual void update () = 0; // call update when 'tabs' change
    virtual bool request (std::intptr_t tab) = 0; // returns false if no such tab exists
    virtual bool request_stack (std::size_t index) = 0;
    virtual bool move_stack (std::intptr_t tab, int index) = 0;
    virtual void stack (std::intptr_t which, std::intptr_t onto, bool after) = 0; // move 'which' tab into stack of 'onto' either right after or as last tab
    virtual HWND addbutton (std::intptr_t id, const wchar_t * text, UINT hint = 0, bool right = false) = 0;
    virtual RECT outline (std::intptr_t tab) = 0;
};

#endif
