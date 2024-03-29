#include "window.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <dwmapi.h>

#include <VersionHelpers.h>

#include "menus.h"
#include "feed.h"
#include "data.h"
#include "../common/errorbox.h"
#include "../common/textscale.h"
#include "editbox.h"
#include "../common/node.h"
#include "resolver.h"

#pragma warning (disable:4996) // GetVersion() warning
#pragma warning (disable:28159) // GetVersion() warning

extern "C" IMAGE_DOS_HEADER __ImageBase;

extern Design design;
extern Cursors cursor;
extern TextScale scale;

namespace {
    ATOM atomCOMBOBOX = 0;
    ATOM atomEDIT = 0;
    ATOM atomSysHeader32 = 0;
    ATOM atomSysListView32 = 0;
    ATOM atomStatusBar = 0;

    void DeferWindowPos (HDWP & hDwp, HWND hCtrl, const RECT & r, UINT flags = 0) {
        hDwp = DeferWindowPos (hDwp, hCtrl, NULL, r.left, r.top, r.right, r.bottom, SWP_NOACTIVATE | SWP_NOZORDER | flags);
    }

    bool IntersectRect (const RECT * r1, const RECT * r2) {
        RECT rTemp;
        return IntersectRect (&rTemp, r1, r2);
    }

    BOOL GetChildRect (HWND hParent, HWND hCtrl, LPRECT rcCtrl) {
        if (GetClientRect (hCtrl, rcCtrl)) {
            MapWindowPoints (hCtrl, hParent, reinterpret_cast <POINT *> (rcCtrl), 2u);
            return TRUE;
        } else
            return FALSE;
    }
    BOOL GetChildRect (HWND hParent, UINT id, LPRECT rcCtrl) {
        return GetChildRect (hParent, GetDlgItem (hParent, id), rcCtrl);
    }
}

bool Window::GetCaptionTextColor (HTHEME, COLORREF & color, UINT & glow) const {
    glow = 2 * this->metrics [SM_CXFRAME] / 2;
    color = GetSysColor (COLOR_WINDOWTEXT);
    
    bool dark = !design.light
             || ((winver == 6) && design.composited && IsZoomed (this->hWnd))
             || ((winver != 7) && (design.prevalence && !design.override.outline) && IsColorDark (design.colorization.active));

    if (this->active) {
        if (dark) {
            color = 0xFFFFFF;
            if (winver == 6) {
                glow = 0;
            }
        }
    } else {
        color = GetSysColor (COLOR_GRAYTEXT);
    }
    return dark;
}

const MARGINS * Window::GetDwmMargins () {
    static MARGINS margins = { 0,0,0,0 };
    margins.cyTopHeight = this->extension + metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];

    if (winver < 10) {
        if (design.nice) {
            margins.cxLeftWidth = dividers.left;
            margins.cxRightWidth = dividers.right + 2;
            margins.cyBottomHeight = minimum.statusbar.cy + 2;
        }
        if (design.composited) {
            margins.cyBottomHeight--;
        }
    }
    if ((design.override.backdrop != DWMSBT_NONE) && (winbuild >= 22543)) {
        margins.cxLeftWidth = dividers.left;
        margins.cxRightWidth = dividers.right;
        margins.cyBottomHeight = minimum.statusbar.cy + 1;
    }

    return &margins;
}

LRESULT Window::OnNcCalcSize (WPARAM wParam, RECT * r) {
    if (!IsIconic (hWnd)) {
        if (r) {
            r->left += metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
            r->right -= metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
            r->bottom -= metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];
        }
        return 0;
    } else
        return DefaultProcedure (hWnd, WM_NCCALCSIZE, wParam, (LPARAM) r);
}

LRESULT Window::OnGetMinMaxInfo (MINMAXINFO * mmi) {
    if (minimum.statusbar.cx || this->tabs.views->minimum.cx) {
        mmi->ptMinTrackSize.x = std::max (minimum.statusbar.cx,
                                            this->tabs.views->minimum.cx + dividers.left + dividers.right)
                                + 2 * (metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER]);
    }
    mmi->ptMinTrackSize.y = this->extension
                            + 2 * (metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER])
                            + minimum.statusbar.cy
                            + this->tabs.feeds->minimum.cy
                            + 6 * this->fonts.text.height;
    return 0;
}

void Window::UpdateListsPosition (HDWP & hDwp, const RECT & client, const RECT & rListTabs) {
    auto r = this->GetTabControlContentRect (this->GetListsFrame (&client, rListTabs));
    r.right -= r.left;
    r.bottom -= r.top;

    for (const auto & tab : this->tabs.lists->tabs) {
        if (tab.second.content)
            DeferWindowPos (hDwp, tab.second.content, r, 0);
    }
}
void Window::UpdateViewsPosition (HDWP & hDwp, const RECT & client) {
    auto r = this->GetTabControlContentRect (this->GetViewsFrame (&client));
    r.right -= r.left;
    r.bottom -= r.top;

    if (design.composited && (winver < 10)) {
        r.right -= 2;
    }

    for (const auto & tab : this->tabs.views->tabs) {
        if (tab.second.content)
            DeferWindowPos (hDwp, tab.second.content, r, 0);
    }
}
void Window::UpdateFeedsPosition (HDWP & hDwp, const RECT & client, const RECT & rFeedsTabs) {
    auto r = this->GetTabControlContentRect (this->GetFeedsFrame (&client, rFeedsTabs));
    r.right -= r.left;
    r.bottom -= r.top;
    
    for (const auto & tab : this->tabs.feeds->tabs) {
        if (tab.second.content)
            DeferWindowPos (hDwp, tab.second.content, r, 0);
    }
}

RECT Window::GetListsTabRect () {
    RECT r;
    r.top = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + this->tabs.views->minimum.cy;
    r.left = 0;

    if (IsAppThemed ()) {
        if (!IsZoomed (hWnd)) {
            if (!design.contrast) {
                r.left = metrics [SM_CXPADDEDBORDER];
            }
            if (winver < 6) {
                r.left = metrics [SM_CXFRAME];
            }
        }
    } else {
        r.top--;
        r.left = 2;
    }

    r.right = dividers.left - r.left - (metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER] / 2);
    r.bottom = this->tabs.lists->minimum.cy + 1;
    return r;
}

RECT Window::GetFeedsTabRect (const RECT * rRightFrame) {
    RECT r;
    r.top = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + LONG (dividers.feeds);
    r.left = rRightFrame->left;
    r.right = rRightFrame->right;
    r.bottom = this->tabs.feeds->minimum.cy;

    if (!IsAppThemed ()) {
        r.left += 2;
        r.right -= 2;
    }
    return r;
}

LRESULT Window::OnPositionChange (const WINDOWPOS & position) {
    if (!(position.flags & SWP_NOSIZE) || (position.flags & (SWP_SHOWWINDOW | SWP_FRAMECHANGED))) {
        if (this->tabs.views && this->tabs.lists && this->tabs.feeds) {

            RECT client;
            if (GetClientRect (hWnd, &client)) {

                if (this->height && client.bottom) {
                    double x;
                    double y = (double) (this->dividers.feeds) * double (client.bottom) / double (this->height);
                    
                    this->ClampDividerRange (0x103, &client, x, y);
                    this->dividers.feeds = y;
                }
                this->height = client.bottom;

                RECT tabs = {
                    dividers.left - 1,
                    metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + 1,
                    0,
                    this->tabs.views->minimum.cy
                };

                RECT buttons;
                if (design.composited
                    && ptrDwmGetWindowAttribute
                    && ptrDwmGetWindowAttribute (hWnd, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof buttons) == S_OK) {

                    tabs.right = buttons.left - dividers.left - metrics [SM_CXFRAME];
                } else {
                    tabs.right = client.right - 3 * metrics [SM_CXSIZE] - dividers.left - metrics [SM_CXFRAME];
                }
                tabs.right = std::min (tabs.right, client.right - dividers.right - tabs.left - metrics [SM_CXFRAME]);
                if (!IsAppThemed ()) {
                    tabs.left += 2;
                    tabs.right -= 2;
                }

                auto hStatusBar = GetDlgItem (hWnd, ID::STATUSBAR);
                auto yStatusBar = UpdateStatusBar (hStatusBar, dpi, client);

                if (HDWP hDwp = BeginDeferWindowPos ((int) (16 + this->tabs.views->tabs.size () + this->tabs.lists->tabs.size ()))) {
                    auto rListTabs = this->GetListsTabRect ();
                    auto rRightPane = this->GetRightPane (&client, rListTabs);
                    auto rFeedsTabs = this->GetFeedsTabRect (&rRightPane);
                    auto rFilters = this->GetFiltersRect (&client, rRightPane);
                    auto rIdentities = rRightPane;

                    if (IsAppThemed ()) {
                        InflateRect (&rFilters, -1, -1);
                    } else {
                        InflateRect (&rFilters, -2, -2);
                    }
                    rFilters.right -= rFilters.left;
                    rFilters.bottom -= rFilters.top;
                    
                    DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::TABS_VIEWS), tabs);
                    DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::TABS_LISTS), rListTabs);
                    DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::TABS_FEEDS), rFeedsTabs);
                    DeferWindowPos (hDwp, hStatusBar, { 0, client.bottom - yStatusBar, client.right, yStatusBar }, SWP_SHOWWINDOW);

                    if (design.nice) {
                        client.bottom -= metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
                    }

                    this->UpdateViewsPosition (hDwp, client);
                    this->UpdateListsPosition (hDwp, client, rListTabs);
                    this->UpdateFeedsPosition (hDwp, client, rFeedsTabs);

                    DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::IDENTITIES), rIdentities);
                    DeferWindowPos (hDwp, GetDlgItem (hWnd, ID::FILTERS), rFilters);

                    EndDeferWindowPos (hDwp);
                }

                if (this->extension != this->tabs.views->minimum.cy + 1) {
                    this->extension = this->tabs.views->minimum.cy + 1;

                    if (ptrDwmExtendFrameIntoClientArea) {
                        ptrDwmExtendFrameIntoClientArea (hWnd, GetDwmMargins ());
                    }
                    InvalidateRect (hWnd, NULL, TRUE);
                }
            }
        }

        if (design.composited && (design.override.backdrop != DWMSBT_NONE) && (winbuild < 22543)) {
            AccentPolicy policy = { 4, 0x01E0, 0xAA232221 /* 0x7FFFAA00 */, 0 };
            if (design.light) {
                policy.gradient = 0xCCE5E5E5;
            }

            CompositionAttributeData data = { WCA_ACCENT_POLICY, &policy, sizeof policy };

            ptrSetWindowCompositionAttribute (this->hWnd, &data);
        }

        if (!design.composited && (this->tabs.views != nullptr)) {
            InvalidateRect (this->tabs.views->hWnd, NULL, TRUE);
            EnumChildWindows (this->tabs.views->hWnd,
                                [](HWND hCtrl, LPARAM)->BOOL {
                                    InvalidateRect (hCtrl, NULL, TRUE);
                                    return TRUE;
                                }, 0);
        }
    }

    WINDOWPLACEMENT placement;
    placement.length = sizeof placement;
    GetWindowPlacement (hWnd, &placement);

    database.windows.update (placement.rcNormalPosition.left, placement.rcNormalPosition.top,
                             placement.rcNormalPosition.right - placement.rcNormalPosition.left,
                             placement.rcNormalPosition.bottom - placement.rcNormalPosition.top,
                             placement.showCmd, this->id);
    return 0;
}

LRESULT Window::OnDpiChange (WPARAM dpi, const RECT * r) {
    dpi = LOWORD (dpi);
    if (this->dpi != dpi) {
        dividers.left = dividers.left * LONG (dpi) / this->dpi;
        dividers.right = dividers.right * LONG (dpi) / this->dpi;
        this->dpi = LONG (dpi);
    }

    OnVisualEnvironmentChange ();
    SetWindowPos (hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, 0);
    return 0;
}

ATOM GetClassAtom (LPCTSTR name) {
    WNDCLASSEX wc {};
    wc.cbSize = sizeof wc;

    return (ATOM) GetClassInfoEx (NULL, name, &wc);
}

void InitializeWindowLayoutGlobals () {
    atomStatusBar = GetClassAtom (L"msctls_statusbar32");

    atomSysListView32 = GetClassAtom (L"SysListView32");
    atomSysHeader32 = GetClassAtom (L"SysHeader32");
    atomCOMBOBOX = GetClassAtom (L"COMBOBOX");
    atomEDIT = GetClassAtom (L"EDIT");
}

BOOL WINAPI UpdateWindowTreeTheme (HWND hCtrl, LPARAM param) {
    EnumChildWindows (hCtrl, UpdateWindowTreeTheme, param);

    if (auto atom = GetClassWord (hCtrl, GCW_ATOM)) {
        /*if (atom == atomStatusBar) {
            SetWindowTheme (hCtrl, design.light ? NULL : L"ExplorerStatusBar", NULL);
        } else*/
        if (atom == atomCOMBOBOX || atom == atomEDIT) {
            SetWindowTheme (hCtrl, design.light ? NULL : L"DarkMode_CFD", NULL);
        } else
        if (atom == atomSysHeader32/* || atom == atomSysListView32*/) {
            SetWindowTheme (hCtrl, design.light ? NULL : L"DarkMode_ItemsView", NULL);
        } else {
            SetWindowTheme (hCtrl, design.light ? NULL : L"DarkMode_Explorer", NULL);
        }
    }
    return TRUE;
}

LRESULT Window::OnVisualEnvironmentChange () {
    this->dpi = GetDPI (this->hWnd);
    auto dpiNULL = GetDPI (NULL);
    auto hTheme = OpenThemeData (hWnd, L"TEXTSTYLE");

    this->fonts.text.Update (hTheme, dpi, dpiNULL, TMT_MSGBOXFONT, nullptr, scale.current, 100);
    this->fonts.tabs.Update (hTheme, dpi, dpiNULL, TMT_CAPTIONFONT);
    this->fonts.tiny.Update (hTheme, dpi, dpiNULL, TMT_MENUFONT, L"Calibri", 7, 8);

    this->fonts.italic.make_italic = true;
    this->fonts.italic.Update (hTheme, dpi, dpiNULL, TMT_MSGBOXFONT, nullptr, scale.current, 100);
    this->fonts.underlined.make_underlined = true;
    this->fonts.underlined.Update (hTheme, dpi, dpiNULL, TMT_MSGBOXFONT, nullptr, scale.current, 100);

    // TODO: Wingdings for some

    if (hTheme) {
        CloseThemeData (hTheme);
    }
    
    if (ptrAllowDarkModeForWindow) {
        ptrAllowDarkModeForWindow (hWnd, true);
    } 
    if (winver >= 11) {
        if ((design.override.backdrop != DWMSBT_NONE) && (winbuild >= 22543)) {
            ptrDwmSetWindowAttribute (hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &design.override.backdrop, sizeof design.override.backdrop);
        }

        if (design.override.outline) {
            COLORREF clr = design.colorization.inactive;
            if ((design.override.backdrop != DWMSBT_NONE) && (winver >= 11)) {
                if (winbuild >= 22543) {
                    clr = DWMWA_COLOR_NONE;
                } else
                if (design.light) {
                    clr = 0x00FFFFFF;
                } else {
                    clr = 0x00000000;
                }
            }
            ptrDwmSetWindowAttribute (hWnd, DWMWA_CAPTION_COLOR, &clr, sizeof clr);
        }
        if (design.override.corners) {
            ptrDwmSetWindowAttribute (hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &design.override.corners, sizeof design.override.corners);
        }
    }

    LONG dark = !design.light;
    if (winver >= 11 || winbuild >= 20161) {
        ptrDwmSetWindowAttribute (hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    } else
    if (winbuild >= 18875) {
        CompositionAttributeData attr = { WCA_USEDARKMODECOLORS, &dark, sizeof dark };
        ptrSetWindowCompositionAttribute (hWnd, &attr);
    } else if (winbuild >= 14393) {
        ptrDwmSetWindowAttribute (hWnd, 0x13, &dark, sizeof dark);
    }

    this->tabs.views->dark = dark;
    this->tabs.lists->dark = dark;
    this->tabs.feeds->dark = dark;

    if (dark) {
        this->tabs.views->style.dark.tab = 0x232221;
        this->tabs.views->style.dark.inactive = 0x232221;
        /* if (design.prevalence && winver == 10) {
            this->tabs.views->style.dark.tab = 0;
        }*/
        this->tabs.views->style.dark.hot = design.colorization.background;
        this->tabs.views->style.dark.current = design.colorization.background;

        this->tabs.lists->style = this->tabs.views->style;
        this->tabs.feeds->style = this->tabs.views->style;
    }

    EnumChildWindows (this->hWnd, UpdateWindowTreeTheme, (LPARAM) 0);
    SetWindowTheme (this->hToolTip, design.light ? NULL : L"DarkMode_Explorer", NULL);

    ListView_SetBkColor (GetDlgItem (this->hWnd, ID::FILTERS), design.colorization.background);
    ListView_SetTextColor (GetDlgItem (this->hWnd, ID::FILTERS), design.colorization.text);

    for (const auto & tab : this->tabs.lists->tabs) {
        ListView_SetBkColor (tab.second.content, design.colorization.background);
        ListView_SetTextColor (tab.second.content, design.colorization.text);
        ListView_SetTextBkColor (tab.second.content, CLR_NONE);
    }
    for (const auto & tab : this->tabs.feeds->tabs) {

    }

    //ListView_SetBkColor (this->lists)

    SendDlgItemMessage (hWnd, ID::TABS_VIEWS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);
    SendDlgItemMessage (hWnd, ID::TABS_FEEDS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);
    SendDlgItemMessage (hWnd, ID::TABS_LISTS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);

    EnumChildWindows (this->tabs.views->hWnd,
                        [](HWND hCtrl, LPARAM font)->BOOL {
                            SendMessage (hCtrl, WM_SETFONT, (WPARAM) font, 1);
                            return TRUE;
                        }, (LPARAM) this->fonts.tabs.handle);

    SendMessage (this->hToolTip, WM_SETFONT, (WPARAM) this->fonts.text.handle, 0);
    SendDlgItemMessage (hWnd, ID::IDENTITIES, WM_SETFONT, (WPARAM) this->fonts.text.handle, 0);
    SendDlgItemMessage (hWnd, ID::FILTERS, WM_SETFONT, (WPARAM) this->fonts.tabs.handle, 0);

    for (const auto & tab : this->tabs.lists->tabs) {
        SendMessage (tab.second.content, WM_SETFONT, (WPARAM) this->fonts.text.handle, 0);
    }
    for (const auto & tab : this->tabs.feeds->tabs) {
        SendMessage (tab.second.content, WM_SETFONT, (WPARAM) this->fonts.text.handle, 0);
    }

    this->RefreshVisualMetrics (dpiNULL);

    for (auto i = 0u; i != (std::size_t) IconSize::Count; ++i) {
        if (auto icon = LoadBestIcon (reinterpret_cast <HINSTANCE> (&__ImageBase), MAKEINTRESOURCE (1),
                                        GetIconMetrics ((IconSize) i, dpiNULL))) {
            if (this->icons [i]) {
                DestroyIcon (this->icons [i]);
            }
            this->icons [i] = icon;
        }
    }
    // TODO: for XP with blue luna theme or dark colored active caption, load white with black outline
    SendMessage (hWnd, WM_SETICON, ICON_SMALL, (LPARAM) this->icons [(std::size_t) IconSize::Small]);
    SendMessage (hWnd, WM_SETICON, ICON_BIG, (LPARAM) this->icons [(std::size_t) ((winver >= 10) ? IconSize::Start : IconSize::Large)]);

        // tc->tabs [301].icon = icons [SmallIconSize];
    this->tabs.views->min_tab_width = 2 * (std::uint16_t) GetIconMetrics (IconSize::Small, dpiNULL).cx;
    this->tabs.views->max_tab_width = 5 * this->tabs.views->min_tab_width; // TODO: settings

    this->tabs.views->dpi = (std::uint16_t) dpi;
    this->tabs.lists->dpi = (std::uint16_t) dpi;
    this->tabs.feeds->dpi = (std::uint16_t) dpi;

    Lists::OnDpiChange (this->tabs.lists, dpi);

    this->lists.channels->OnDpiChange (dpi);
    this->lists.identities->OnDpiChange (dpi);

    SetWindowPos (hWnd, NULL, 0,0,0,0, SWP_FRAMECHANGED | SWP_DRAWFRAME | SWP_NOREPOSITION | SWP_NOSIZE | SWP_NOMOVE);
    return 0;
}

LRESULT Window::OnNonClientActivate (UINT message, WPARAM wParam, LPARAM lParam) {
    this->active = wParam;
    auto rv = DefaultProcedure (hWnd, message, wParam, lParam);

    UINT redraw = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_ERASENOW | RDW_UPDATENOW;
    if (wParam) {
        redraw |= RDW_FRAME;
    }
    RedrawWindow (hWnd, NULL, NULL, redraw);
    return rv;
}
        
LRESULT Window::OnNonClientHitTest (UINT message, WPARAM wParam, LPARAM lParam) {
    BOOL fromDWM = FALSE;
    auto result = DefaultProcedure (hWnd, message, wParam, lParam, &fromDWM);
    POINT pt = { (signed short) LOWORD (lParam), (signed short) HIWORD (lParam) };

    switch (result) {
        case HTCLOSE:
        case HTMAXBUTTON:
        case HTMINBUTTON:
            if (design.composited && !fromDWM) {
                result = HTCLIENT;
            }
            break;
                
        case HTCLIENT:
        case HTBORDER:
            RECT r;
            GetClientRect (hWnd, &r);
            ScreenToClient (hWnd, &pt);

            if (pt.y <= metrics [SM_CYFRAME] + metrics [SM_CYBORDER]) {
                result = HTTOP;
            } else
            if (pt.y < this->extension + metrics [SM_CYFRAME]) {

                if (pt.y < (metrics [SM_CYFRAME] + metrics [SM_CYCAPTION])
                    && pt.x < (metrics [SM_CXFRAME] + metrics [SM_CXSMICON])) {

                    result = HTSYSMENU;
                } else
                if (!design.composited && pt.y < metrics [SM_CYCAPTION] + metrics [SM_CYFRAME]) {

                    if (pt.x > r.right - 1 * metrics [SM_CXSIZE])
                        result = HTCLOSE;
                    else
                    if (pt.x > r.right - 2 * metrics [SM_CXSIZE])
                        result = HTMAXBUTTON;
                    else
                    if (pt.x > r.right - 3 * metrics [SM_CXSIZE])
                        result = HTMINBUTTON;
                    else
                        result = HTCAPTION;
                } else {
                    result = HTCAPTION;
                }
            } else
            if (pt.x > (r.right - (metrics [SM_CXSMICON] + metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER]))
                && pt.y > (r.bottom - (metrics [SM_CYSMICON] + metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER]))) {
                result = HTBOTTOMRIGHT;
            } else
            if (pt.x <= metrics [SM_CXPADDEDBORDER]) {
                if (!design.contrast && !IsZoomed (hWnd) && IsAppThemed ()) {
                    result = HTLEFT;
                }
            } else
            if (pt.x >= (r.right - metrics [SM_CXPADDEDBORDER])) {
                if (!design.contrast && !IsZoomed (hWnd) && IsAppThemed ()) {
                    result = HTRIGHT;
                }
            }
    }
    return result;
}

template <typename T>
void Window::ClampDividerRange (int id, const RECT * client, T & x, T & y) {

    const auto w = metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
    const auto h = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];

    switch (id) {
        case 0x201:
            if (x > (client->right - this->dividers.right) - w) {
                x = (client->right - this->dividers.right) - w;
            }
            if (x < w) {
                x = w;
            }
            break;
        case 0x202:
            if (x > client->right) {
                x = client->right;
            }
            if (x < this->dividers.left + w) {
                x = this->dividers.left + w;
            }
            break;
        case 0x103:
            if (y > client->bottom - this->tabs.feeds->minimum.cy - this->extension) {
                y = client->bottom - this->tabs.feeds->minimum.cy - this->extension;
            }
            if (y < this->extension + 4 * this->fonts.text.height) {
                y = this->extension + 4 * this->fonts.text.height;
            }
            break;
    }
}

LRESULT Window::OnMouse (UINT message, WPARAM modifiers, LONG x, LONG y) {
    RECT client;
    GetClientRect (hWnd, &client);

    auto element = 0;
    auto distance = 0;
    const auto w = metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
    const auto h = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];

    if (GetCapture () == hWnd) {
        switch (message) {
            case WM_LBUTTONUP:
                this->drag.what = false;
                ReleaseCapture ();
                break;

            case WM_MOUSEMOVE:
                element = this->drag.what;
                this->ClampDividerRange (element, &client, x, y);

                switch ((element & 0x0F00) >> 8) { // compute distance from previous position
                    case 2:
                        distance = (x - this->drag.x);
                        this->drag.x = x;
                        break;
                    case 1:
                        distance = (y - this->drag.y);
                        this->drag.y = y;
                        break;
                }

                if (distance) {
                    switch (element) { // move
                        case 0x201:
                            dividers.left = dividers.left + distance;
                            if (dividers.left < metrics [SM_CXICON]) {
                                dividers.left = metrics [SM_CXICON];
                            }
                            break;
                        case 0x202:
                            dividers.right = dividers.right - distance;
                            if (dividers.right < 0) {
                                dividers.right = 0;
                            }
                            break;
                        case 0x103:
                            dividers.feeds = dividers.feeds + distance;
                            break;
                    }
                    // redraw
                    this->OnPositionChange (WINDOWPOS ());
                    if (ptrDwmExtendFrameIntoClientArea) {
                        ptrDwmExtendFrameIntoClientArea (hWnd, this->GetDwmMargins ());
                    }

                    RedrawWindow (hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);

                    if (!design.composited) {
                        switch (element) {
                            case 0x103:
                                RedrawWindow (GetDlgItem (hWnd, ID::TABS_FEEDS), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                                break;
                        }
                    }
                }
                break;
        }
    } else {
        auto top = metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER] + this->tabs.views->minimum.cy;
        auto bottom = client.bottom - minimum.statusbar.cy;

        if ((y > top) && (y < bottom)) {
            if (x >= dividers.left - w && x <= dividers.left) {
                element = 0x201;
            }
            if (dividers.right > 0) {
                if (x >= (client.right - dividers.right) && x <= (client.right - dividers.right) + w) {
                    element = 0x202;
                }
            } else {
                dividers.right = 0;
            }
        }

        if ((dividers.right > 0) && (x > (client.right - dividers.right) + w)) {
            if (y >= dividers.feeds && y <= (dividers.feeds + h)) {
                element = 0x103;
            }
        }

        if (element) {
            switch (message) {
                case WM_LBUTTONDOWN:
                    this->drag.what = element;
                    this->drag.x = x;
                    this->drag.y = y;
                    SetCapture (hWnd);
                    break;
                case WM_LBUTTONDBLCLK:
                    switch (element) {
                        case 0x202:
                            this->OnCommand (BN_CLICKED, 0xF4, NULL);
                            break;
                    }
                    break;
            }
        }
    }

    switch (message) {
        case WM_MOUSEMOVE:
            switch ((element & 0x0F00) >> 8) {
                case 2: SetCursor (cursor.horizontal); break;
                case 1: SetCursor (cursor.vertical); break;
                case 0: SetCursor (cursor.arrow); break;
            }
            break;
    }
    return 0;
}
    
// TODO: computing proper metrics for various Windows versions and themes below is a mess;
//       it might be nice to split into virtual classes simply switched on wm_themechange
//       (at least windows version really doesn't need to be checked dozen times per redraw)

void Window::BackgroundFill (HDC hDC, RECT rcArea, const RECT * rcClip, bool fromControl) {
    if (design.composited) {

        auto margins = this->GetDwmMargins ();
        RECT face = {
            rcArea.left + margins->cxLeftWidth,
            rcArea.top + margins->cyTopHeight,
            rcArea.right - margins->cxRightWidth,
            rcArea.bottom - margins->cyBottomHeight,
        };
        auto top = rcArea.top;
        auto transparent = (HBRUSH) GetStockObject (BLACK_BRUSH);

        if (margins->cyTopHeight) { RECT r = { rcClip->left, top, rcClip->right, face.top }; FillRect (hDC, &r, transparent); }
        if (margins->cxLeftWidth) { RECT r = { rcClip->left, top, face.left, rcClip->bottom }; FillRect (hDC, &r, transparent); }
        if (margins->cxRightWidth) { RECT r = { face.right, top, rcClip->right, rcClip->bottom }; FillRect (hDC, &r, transparent); }
        if (margins->cyBottomHeight) { RECT r = { rcClip->left, face.bottom, rcClip->right, rcClip->bottom }; FillRect (hDC, &r, transparent); }

        IntersectRect (&face, &face, rcClip);

        if (design.override.backdrop != DWMSBT_NONE) {
            FillRect (hDC, &face, transparent);
        } else {
            if (this->active && !design.override.outline) {
                SetDCBrushColor (hDC, design.colorization.active & 0x00FFFFFF);
            } else {
                SetDCBrushColor (hDC, design.colorization.inactive & 0x00FFFFFF);
            }
            FillRect (hDC, &face, (HBRUSH) GetStockObject (DC_BRUSH));
        }

        // our TabControl expects composited design background color to be left in DC Brush
        //  - currently only present on Windows 11 where, for some reason, the 0x00000000 isn't
        //    painting transparent, so as temporary fix I use the outline color of tabs which
        //    draws them not-rounded again; TODO: fix

        if (design.override.backdrop != DWMSBT_NONE) {
            if (design.light) {
                SetDCBrushColor (hDC, 0x00E5E5E5);
            } else {
                SetDCBrushColor (hDC, 0x00232221);
            }
        }
    } else {
        if (HANDLE hTheme = OpenThemeData (hWnd, VSCLASS_WINDOW)) {
            UINT type = this->active ? FS_ACTIVE : FS_INACTIVE;

            RECT rcLeft = {
                rcArea.left - metrics [SM_CXFRAME],
                rcArea.top + metrics [SM_CYFRAME] + metrics [SM_CYCAPTION],
                rcArea.left + dividers.left,
                rcArea.bottom
            };
            RECT rcRight = {
                rcArea.left + dividers.left,
                rcArea.top + metrics [SM_CYFRAME] + metrics [SM_CYCAPTION],
                rcArea.right + metrics [SM_CXFRAME],
                rcArea.bottom
            };
            if (winver >= 6) {
                DrawThemeBackground (hTheme, hDC, WP_FRAMELEFT, type, &rcLeft, rcClip);
                DrawThemeBackground (hTheme, hDC, WP_FRAMERIGHT, type, &rcRight, rcClip);
            } else {
                FillRect (hDC, &rcLeft, GetSysColorBrush (COLOR_3DFACE));
                FillRect (hDC, &rcRight, GetSysColorBrush (COLOR_3DFACE));
            }

            if (fromControl) {
                RECT rcCaption = rcArea;
                rcCaption.bottom = rcCaption.top + metrics [SM_CYFRAME] + metrics [SM_CYCAPTION];
                rcCaption.left -= metrics [SM_CXFRAME];
                rcCaption.right += metrics [SM_CXFRAME];

                if (IsZoomed (hWnd))
                    rcCaption.top += metrics [SM_CYFRAME];

                if (IntersectRect (&rcCaption, rcClip))
                    DrawThemeBackground (hTheme, hDC, IsZoomed (hWnd) ? WP_MAXCAPTION : WP_CAPTION, type, &rcCaption, rcClip);
            }

            CloseThemeData (hTheme);
        } else {
            RECT rcFill = rcArea;
            if (!fromControl) {
                rcFill.top = metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
            }

            RECT rcIntersection;
            if (IntersectRect (&rcIntersection, &rcFill, rcClip)) {
                FillRect (hDC, &rcIntersection, GetSysColorBrush (COLOR_BTNFACE));
            }

            RECT rcCaption = rcArea;
            rcCaption.top += metrics [SM_CYFRAME];
            rcCaption.bottom = rcCaption.top + metrics [SM_CYCAPTION];

            if (IntersectRect (&rcCaption, rcClip)) {
                BOOL gradient = FALSE;
                SystemParametersInfo (SPI_GETGRADIENTCAPTIONS, 0, &gradient, 0);

                DrawCaption (hWnd, hDC, &rcCaption, DC_TEXT | DC_ICON | (gradient ? DC_GRADIENT : 0) | (this->active ? DC_ACTIVE : 0));

                auto w = metrics [SM_CXSIZE] - 2;
                auto h = metrics [SM_CYSIZE] - 4;

                RECT r;
                r.top = rcCaption.top + (rcCaption.bottom - rcCaption.top) / 2 - h / 2;
                r.bottom = r.top + h;
                r.right = rcCaption.right - 2;
                r.left = r.right - w;

                DrawFrameControl (hDC, &r, DFC_CAPTION, DFCS_CAPTIONCLOSE);
                OffsetRect (&r, -(w + 2), 0);
                DrawFrameControl (hDC, &r, DFC_CAPTION, IsZoomed (hWnd) ? DFCS_CAPTIONRESTORE : DFCS_CAPTIONMAX);
                OffsetRect (&r, -w, 0);
                DrawFrameControl (hDC, &r, DFC_CAPTION, DFCS_CAPTIONMIN);
            }
        }
    }

    auto rListTabs = this->GetListsTabRect ();
    auto rList = GetListsFrame (&rcArea, rListTabs);
    auto rRight = GetRightPane (&rcArea, rListTabs);
    auto rFeeds = GetFeedsFrame (&rcArea, this->GetFeedsTabRect (&rRight));
    auto rFilters = GetFiltersRect (&rcArea, rRight);

    if (!design.light) {
        SelectObject (hDC, GetStockObject (DC_PEN));
        SelectObject (hDC, GetStockObject (NULL_BRUSH));
        SetDCPenColor (hDC, GetSysColor (COLOR_3DDKSHADOW));

        auto hPen = CreatePenEx (PS_SOLID, 1, 0x9F000000 | GetSysColor (COLOR_3DSHADOW));
        auto hOldPen = SelectObject (hDC, hPen);

        auto rPane = GetViewsFrame (&rcArea);
        auto rPaneClip = GetTabControlClipRect (rPane);
        Rectangle (hDC, rPaneClip.left, rPaneClip.top, rPaneClip.right, rPaneClip.bottom);

        Rectangle (hDC, rList.left, rList.top, rList.right, rList.bottom);
        Rectangle (hDC, rFeeds.left, rFeeds.top, rFeeds.right, rFeeds.bottom);
        Rectangle (hDC, rFilters.left, rFilters.top, rFilters.right, rFilters.bottom);

        SelectObject (hDC, hOldPen);
        DeleteObject (hPen);

    } else
    if (HANDLE hTheme = OpenThemeData (hWnd, VSCLASS_TAB)) {
        if ((winver >= 10) || !design.composited) {
            auto rPane = GetViewsFrame (&rcArea);
            auto rPaneClip = GetTabControlClipRect (rPane);
            DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rPane, &rPaneClip);
        }

        auto rListClip = GetTabControlClipRect (rList);
        DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rList, &rListClip);
        auto rFeedsClip = GetTabControlClipRect (rFeeds);
        DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rFeeds, &rFeedsClip);
        auto rFiltersClip = GetTabControlClipRect (rFilters);

        if (winver < 6) {
            rFilters.right += 2;
            rFilters.bottom += 2;
            rFiltersClip.right += 2;
            rFiltersClip.bottom += 1;
        }

        DrawThemeBackground (hTheme, hDC, TABP_PANE, 0, &rFilters, &rFiltersClip);// */
        CloseThemeData (hTheme);
        
    } else {
        auto rPane = GetViewsFrame (&rcArea);

        DrawEdge (hDC, &rPane, BDR_SUNKEN, BF_RECT);
        DrawEdge (hDC, &rList, BDR_SUNKEN, BF_RECT);
        rFeeds.top += 1;
        DrawEdge (hDC, &rFeeds, BDR_SUNKEN, BF_RECT);
        DrawEdge (hDC, &rFilters, BDR_SUNKEN, BF_RECT);
    }
}

RECT Window::GetViewsFrame (const RECT * rcArea) {
    RECT r = *rcArea;
    r.top += this->GetDwmMargins ()->cyTopHeight - 1;
    r.left += dividers.left - 1;
    r.right -= dividers.right - 1;
    r.bottom -= minimum.statusbar.cy;

    if (IsAppThemed ()) {
        if (!IsZoomed (hWnd)) {
            if (!design.contrast) {
                if (!dividers.right) {
                    r.right -= metrics [SM_CXPADDEDBORDER];
                }
            }
        }
        if (winver < 6) {
            r.bottom -= metrics [SM_CYFRAME];
        }
    } else {
        r.top -= 1;
    }
    return r;
}

RECT Window::GetTabControlContentRect (RECT r) {
    if (IsAppThemed ()) {
        r = GetTabControlClipRect (r);
        r.bottom += metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
        InflateRect (&r, -1, -1);
        if (winver < 6) {
            r.bottom -= this->extension + metrics [SM_CYFRAME] - 2; // shadow
        }
    } else {
        InflateRect (&r, -2, -2);
    }
    return r;
}

RECT Window::GetAdjustedFrame (RECT r, LONG top, const RECT & rTabs) {
    r.top += top + this->tabs.views->minimum.cy - 1;
    r.left += rTabs.left;
    r.right = r.left + rTabs.right;
    r.bottom -= minimum.statusbar.cy;

    if (IsAppThemed ()) {
        if (winver < 6) {
            r.bottom -= metrics [SM_CYFRAME];
        }
    } else {
        r.top -= 2;
        r.left -= 2;
    }
    return r;
}

RECT Window::GetListsFrame (const RECT * rcArea, const RECT & rListTabs) {
    return this->GetAdjustedFrame (*rcArea, this->GetDwmMargins ()->cyTopHeight, rListTabs);
}

RECT Window::GetFeedsFrame (const RECT * rcArea, const RECT & rFeedsTabs) {
    return this->GetAdjustedFrame (*rcArea, rFeedsTabs.top, rFeedsTabs);
}

RECT Window::GetRightPane (const RECT * client, const RECT & rListTabs) {
    RECT r = rListTabs;
    r.left = (client->right - client->left) - dividers.right + metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER] / 2;
    r.right = (client->right - client->left) - r.left;
    if (!IsZoomed (hWnd) && !design.contrast) {
        r.right -= metrics [SM_CXPADDEDBORDER];

        if ((winver < 6) && IsAppThemed ()) {
            r.right -= metrics [SM_CXFRAME];
        }
        if ((winver == 6 || winver == 7) && IsAppThemed () && !design.composited) {
            r.right -= 2;
        }
    } else {
        r.right--;
    }
    return r;
}

RECT Window::GetFiltersRect (const RECT * rcArea, const RECT & rRightPane) {
    RECT r = *rcArea;
    r.bottom = r.top + LONG (dividers.feeds) - 1;
    r.top += rRightPane.top + /*this->tabs.views->minimum.cy*/ this->fonts.text.height + 4 * this->metrics [SM_CYFIXEDFRAME] - 1 + metrics [SM_CYFRAME];
    r.left += rRightPane.left;
    r.right = r.left + rRightPane.right;
    return r;
}

RECT Window::GetTabControlClipRect (RECT & rX) {
    RECT r = rX;
    if (winver == 5 && IsAppThemed ()) {
        r.right -= 2;
        r.bottom -= 1;
    }
    if (winver >= 6 && !design.contrast) {
        rX.right += 2;
        rX.bottom += 1;
    }
    return r;
}

LRESULT Window::OnDrawItem (WPARAM id, DRAWITEMSTRUCT * draw) {
    switch (id) {
        case ID::STATUSBAR:
            DrawCompositedTextOptions options;
            options.font = this->fonts.text.handle;
            options.theme = GetWindowTheme (draw->hwndItem);

            this->GetCaptionTextColor (NULL, options.color, options.glow);

            if (!design.composited) {
                options.glow = 0;
            }

            draw->rcItem.top += 1;
            draw->rcItem.left += this->metrics [SM_CXPADDEDBORDER];
            draw->rcItem.right -= this->metrics [SM_CXFIXEDFRAME];
            if (SendMessage (draw->hwndItem, SB_GETICON, draw->itemID, 0)) {
                draw->rcItem.left += this->metrics [SM_CXSMICON] + this->metrics [SM_CXFIXEDFRAME];
            }

            SetBkMode (draw->hDC, TRANSPARENT);
            DrawCompositedText (draw->hDC, (LPWSTR) draw->itemData, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, draw->rcItem, &options);
            return TRUE;
    }
    return FALSE;
}

LRESULT Window::OnControlPrePaint (HDC hDC, HWND hControl) {
    RECT rWindow;
    RECT rControl;

    GetWindowRect (hWnd, &rWindow);
    GetWindowRect (hControl, &rControl);

    OffsetRect (&rWindow, -rControl.left, -rControl.top);
    OffsetRect (&rControl, -rControl.left, -rControl.top);
    rWindow.left += metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
    rWindow.right -= metrics [SM_CXFRAME] + metrics [SM_CXPADDEDBORDER];
    rWindow.bottom -= metrics [SM_CYFRAME] + metrics [SM_CXPADDEDBORDER];

    this->BackgroundFill (hDC, rWindow, &rControl, true);
    return (LRESULT) GetStockObject (NULL_BRUSH);
}

LRESULT Window::OnPaint () {
    PAINTSTRUCT ps;
    if (HDC hScreenDC = BeginPaint (hWnd, &ps)) {
        HDC hDC = NULL;
        HPAINTBUFFER hBuffer = NULL;

        RECT client;
        GetClientRect (hWnd, &client);

        if (ptrBeginBufferedPaint) {
            static const BP_PAINTPARAMS erase = { sizeof (BP_PAINTPARAMS), BPPF_ERASE, NULL, NULL };
            RECT r = client;
            if (design.nice && !design.composited) {
                r.top = metrics [SM_CYCAPTION] + metrics [SM_CYFRAME];
            }
            if (!IsAppThemed ()) {
                r.top = metrics [SM_CYFRAME];
            }
            hBuffer = ptrBeginBufferedPaint (hScreenDC, &r, BPBF_TOPDOWNDIB, &erase, &hDC);
        }
        if (!hBuffer) {
            hDC = hScreenDC;
        }

        this->BackgroundFill (hDC, client, &ps.rcPaint, false);

        if (design.composited) {
            POINT iconPos = { 0, metrics [SM_CYFRAME] };
            SIZE iconSize = GetIconMetrics (IconSize::Start);

            if (winver >= 10) {
                if (IsZoomed (hWnd) || design.contrast) {
                    iconPos.y += metrics [SM_CXPADDEDBORDER];
                } else {
                    iconPos.x += metrics [SM_CXPADDEDBORDER];
                }
            }

            if (HANDLE hTheme = OpenThemeData (hWnd, VSCLASS_WINDOW)) {
                COLORREF color = 0xFF00FF;
                UINT glow;

                if (this->GetCaptionTextColor (hTheme, color, glow)) { // text is bright
                    for (auto & tc : this->alltabs) {
                        tc->buttons.color = 0xD0D0D0;
                        tc->buttons.hot = 0xFFFFFF;
                    }
                } else {
                    for (auto & tc : this->alltabs) {
                        tc->buttons.color = color;
                        tc->buttons.hot = GetSysColor (COLOR_HOTLIGHT);
                    }
                }
                for (auto & tc : this->alltabs) {
                    tc->buttons.down = 0x7F7F7F;
                    tc->buttons.glow = glow;
                }

                const wchar_t * title = nullptr;
                const auto title_length = LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase), 1, (wchar_t *) &title, 0);
                const wchar_t * subtitle = nullptr;
                const auto subtitle_length = LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase), 2, (wchar_t *) &subtitle, 0);

                RECT r;
                r.top = iconPos.y;
                r.left = iconPos.x + metrics [SM_CXFRAME] + iconSize.cx;
                r.right = dividers.left - metrics [SM_CXFRAME];
                r.bottom = r.top + this->extension - metrics [SM_CYFRAME] / 2;

                DrawCompositedTextDIB (hDC, hTheme, this->fonts.tiny.handle, subtitle, subtitle_length,
                                       DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, r, color, glow);
                DrawCompositedTextDIB (hDC, hTheme, this->fonts.tabs.handle, title, title_length,
                                       DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS, r, color, glow);

                CloseThemeData (hTheme);
            }
            DrawIconEx (hDC, iconPos.x, iconPos.y, this->icons [(std::size_t) IconSize::Start], 0, 0, 0, NULL, DI_NORMAL);
        }

        // TODO: draw button floating over right edge of content (show/hide right panel)

        // TODO: draw horizontal splitter grips
        // TODO: draw bottom right size grip
        if (design.nice) {

        }

        if (hBuffer) {
            ptrEndBufferedPaint (hBuffer, TRUE);
        }

        EndPaint (hWnd, &ps);
    }
    return 0;
}
