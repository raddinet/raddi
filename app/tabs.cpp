#include "tabs.h"
#include <VersionHelpers.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <algorithm>

#include <vector>
#include <string>

namespace {
    LRESULT CALLBACK Procedure (HWND, UINT, WPARAM, LPARAM);
    unsigned int UpdatePresentation (HWND hWnd);

    struct Tab {
        std::wstring text;
        LONG_PTR     id;
        bool         close;

        // computed
        bool left;
        bool right;
        RECT r;
        RECT rCloseButton;
    };

    enum class Index {
        Data, // vector<Tab>
        Font, // HFONT
        Theme,
        WndTheme,
        Offset,  // first tab displayed
        Current, // current active tab
        Hot,
        Pressed, // which close button is pressed

        DPI,
        MinHeight, // computed
        WheelDelta,

        Size
    };

    LONG_PTR Get (HWND hWnd, Index index) {
        return GetWindowLongPtr (hWnd, int (index) * sizeof (LONG_PTR));
    }
    LONG_PTR Set (HWND hWnd, Index index, LONG_PTR data) {
        return SetWindowLongPtr (hWnd, int (index) * sizeof (LONG_PTR), data);
    }

    HANDLE  (WINAPI * ptrBeginBufferedPaint) (HDC, LPCRECT, BP_BUFFERFORMAT, BP_PAINTPARAMS *, HDC *) = NULL;
    HRESULT (WINAPI * ptrEndBufferedPaint) (HANDLE, BOOL) = NULL;
    HRESULT (WINAPI * ptrBufferedPaintSetAlpha) (HANDLE, LPCRECT, BYTE) = NULL;

    template <typename P>
    void Symbol (HMODULE h, P & pointer, const char * name) {
        pointer = reinterpret_cast <P> (GetProcAddress (h, name));
    }
    RECT GetClientRect (HWND hWnd) {
        RECT r;
        if (!GetClientRect (hWnd, &r)) {
            std::memset (&r, 0, sizeof r);
        }
        return r;
    }
}

ATOM InitializeTabControl (HINSTANCE hInstance) {
    if (HMODULE hUxTheme = GetModuleHandle (L"UXTHEME")) {
        Symbol (hUxTheme, ptrBeginBufferedPaint, "BeginBufferedPaint");
        Symbol (hUxTheme, ptrEndBufferedPaint, "EndBufferedPaint");
        Symbol (hUxTheme, ptrBufferedPaintSetAlpha, "BufferedPaintSetAlpha");
    }

    WNDCLASS wc;

    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Procedure;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = int (Index::Size) * sizeof (LONG_PTR);
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor (NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = TEXT ("RADDI:TabControl");

    return RegisterClass (&wc);
}

HWND CreateTabControl (HINSTANCE hInstance, HWND hParent, UINT style, UINT id) {
    return CreateWindowEx (WS_EX_NOPARENTNOTIFY, TEXT ("RADDI:TabControl"), TEXT (""),
                           style | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                           0,0,0,0, hParent, (HMENU) (std::intptr_t) id, hInstance, NULL);
}

namespace {
    LRESULT OnCreate (HWND, const CREATESTRUCT *);
    LRESULT OnDestroy (HWND);
    LRESULT OnPaint (HWND, HDC, RECT);
    LRESULT OnClick (HWND, UINT, SHORT, SHORT);
    LRESULT OnHitTest (HWND, SHORT, SHORT);
    LRESULT OnMouseMove (HWND, SHORT, SHORT);
    LRESULT OnKeyDown (HWND, WPARAM, LPARAM);
    LRESULT OnHorizontalWheel (HWND, LONG, USHORT);
    LRESULT OnDataOperation (HWND, UINT, WPARAM, LPARAM);

    void Left (HWND);
    void Right (HWND);
    void Right (HWND, const std::vector <Tab> * tabs, ULONG_PTR &, const RECT *);
    void Next (HWND);
    void Previous (HWND);

    LRESULT CALLBACK Procedure (HWND hWnd, UINT message,
                                WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_CREATE:
                OnDataOperation (hWnd, message, wParam, lParam);
                return OnCreate (hWnd, reinterpret_cast <const CREATESTRUCT *> (lParam));
            case WM_DESTROY:
                return OnDestroy (hWnd);

            case WM_SETFONT:
                Set (hWnd, Index::Font, wParam);
                if (lParam) {
                    UpdatePresentation (hWnd);
                }
                break;
            case WM_GETFONT:
                return (LRESULT) Get (hWnd, Index::Font);

            case WM_SIZE:
                UpdatePresentation (hWnd);
                return 0;

            case WM_PAINT: {
                PAINTSTRUCT ps;
                if (HDC hDC = BeginPaint (hWnd, &ps)) {
                    OnPaint (hWnd, hDC, ps.rcPaint);
                }
                EndPaint (hWnd, &ps);
            } break;

            case WM_PRINTCLIENT:
                return OnPaint (hWnd, (HDC) wParam, GetClientRect (hWnd));

            case WM_NCHITTEST:
                return OnHitTest (hWnd, LOWORD (lParam), HIWORD (lParam));

            case WM_MOUSEMOVE:
                return OnMouseMove (hWnd, LOWORD (lParam), HIWORD (lParam));
            case WM_MOUSELEAVE:
                return OnMouseMove (hWnd, -1, -1);

            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
                return OnClick (hWnd, message, LOWORD (lParam), HIWORD (lParam));
            case WM_KEYDOWN:
                return OnKeyDown (hWnd, wParam, lParam);

            case WM_MOUSEHWHEEL:
                return OnHorizontalWheel (hWnd, (LONG) (SHORT) HIWORD (wParam), LOWORD (wParam));

            case WM_CHAR: {
                // TODO: switch to tab by the letter or number
                NMCHAR nm = {
                    { hWnd, (UINT) GetDlgCtrlID (hWnd), (UINT) NM_CHAR },
                    (UINT) wParam, 0u, 0u
                };
                SendMessage (GetParent (hWnd), WM_NOTIFY, nm.hdr.idFrom, (LPARAM) &nm);
            } break;

            case WM_SETFOCUS:
            case WM_KILLFOCUS:
            case WM_UPDATEUISTATE:
            case WM_SYSCOLORCHANGE:
            case WM_SETTINGCHANGE:
                InvalidateRect (hWnd, NULL, FALSE);
                break;

            case WM_THEMECHANGED:
                if (auto old = (HTHEME) Set (hWnd, Index::Theme, (LONG_PTR) OpenThemeData (hWnd, VSCLASS_TAB))) CloseThemeData (old);
                if (auto old = (HTHEME) Set (hWnd, Index::WndTheme, (LONG_PTR) OpenThemeData (hWnd, VSCLASS_WINDOW))) CloseThemeData (old);

                InvalidateRect (hWnd, NULL, FALSE);
                break;

            case WM_GETDLGCODE:
                switch (wParam) {
                    case VK_LEFT:
                    case VK_RIGHT:
                        return DLGC_WANTARROWS;
                }
                break;

            case WM_USER + 3:
                return Get (hWnd, Index::MinHeight);
            case WM_USER + 4:
                return Set (hWnd, Index::DPI, wParam);

            case TCM_INSERTITEM:
            case TCM_SETCURSEL:
            case TCM_GETCURSEL:
            case TCM_GETITEM:
            case TCM_GETITEMCOUNT:
            case TCM_GETITEMRECT:
            case TCM_DELETEITEM:
            case TCM_DELETEALLITEMS:
                return OnDataOperation (hWnd, message, wParam, lParam);

            default:
                return DefWindowProc (hWnd, message, wParam, lParam);
        }
        return 0;
    }

    LRESULT OnDataOperation (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        try {
            Tab tab;
            auto tabs = reinterpret_cast <std::vector <Tab> *> (Get (hWnd, Index::Data));
            auto current = (ULONG_PTR) Get (hWnd, Index::Current);
            bool update = false;

            switch (message) {
                case WM_CREATE:
                    Set (hWnd, Index::Data, (LONG_PTR) new std::vector <Tab>);
                    break;

                case TCM_INSERTITEM: // wParam = ID, lParam = LPCWSTR, returns index, replaces existing
                    tab.id = wParam;
                    if (auto psz = reinterpret_cast <const wchar_t *> (lParam)) {
                        if (psz [0] == 0x0001) {
                            tab.text = psz + 1;
                            tab.close = false;
                        } else {
                            tab.text = psz;
                            tab.close = true;
                        }
                    }
                    tabs->push_back (tab);
                    return tabs->size ();

                case TCM_DELETEALLITEMS:
                    tabs->clear ();
                    Set (hWnd, Index::Offset, 0);
                    UpdatePresentation (hWnd);
                    return TRUE;

                 case TCM_GETITEM:
                    return tabs->at (wParam).id;
                case TCM_GETITEMCOUNT:
                    return tabs->size ();
                case TCM_GETITEMRECT: // wParam = index, lparam = RECT*
                    if (auto * r = reinterpret_cast <RECT *> (lParam)) {
                        *r = tabs->at (wParam).r;
                    }
                    return TRUE;

                case TCM_GETCURSEL:
                    if (auto * ptrID = reinterpret_cast <LONG_PTR *> (lParam)) {
                        *ptrID = tabs->at (current).id;
                    }
                    return current;

                    // TODO: some BringToView func

                // TODO: find ID
                // TODO: setcursel/delete: lParam != NULL then lParam -> ID

                /*case TCM_DELETEITEM:
                    if (wParam < tabs->size ()) {

                        if (wParam < current) {
                            Set (hWnd, Index::Current, current - 1);
                        }
                        if (wParam == current) {

                        }

                        tabs->erase (tabs->begin () + wParam);
                        UpdatePresentation (hWnd);

                        if (wParam <= current) {
                            // NOTIFY
                        }
                    }
                    return FALSE;*/

                /*case TCM_SETCURSEL:
                    for (auto i = 0u; i != tabs->size (); ++i) {
                        if ((*tabs) [i].id == wParam) {
                            Set (hWnd, Index::Current, i);

                            // TODO: scroll to view

                            UpdatePresentation (hWnd);

                            // TODO: NOTIFY
                            return i;
                        }
                    }
                    return -1;

                    if (wParam < tabs->size ())
                        return (*tabs) [wParam].id;
                    else
                        return 0;
*/
            }
        } catch (const std::bad_alloc &) {
            NMHDR nm = { hWnd, (UINT) GetDlgCtrlID (hWnd), (UINT) NM_OUTOFMEMORY };
            SendMessage (GetParent (hWnd), WM_NOTIFY, nm.idFrom, (LPARAM) &nm);

        } catch (const std::out_of_range &) {
        } catch (const std::exception &) {
        }
        return 0;
    }

    LRESULT OnCreate (HWND hWnd, const CREATESTRUCT * cs) {
        Set (hWnd, Index::Hot, -1);
        Set (hWnd, Index::Pressed, -1);
        Set (hWnd, Index::Theme, (LONG_PTR) OpenThemeData (hWnd, VSCLASS_TAB));
        Set (hWnd, Index::WndTheme, (LONG_PTR) OpenThemeData (hWnd, VSCLASS_WINDOW));

        SendMessage (hWnd, WM_CHANGEUISTATE, UIS_INITIALIZE, 0u);
        return 0;
    }

    LRESULT OnDestroy (HWND hWnd) {
        if (auto theme = (HTHEME) Get (hWnd, Index::Theme)) CloseThemeData (theme);
        if (auto theme = (HTHEME) Get (hWnd, Index::WndTheme)) CloseThemeData (theme);

        delete reinterpret_cast <std::vector <Tab> *> (Get (hWnd, Index::Data));
        return 0;
    }

    unsigned int UpdatePresentation (HWND hWnd) {

        // TODO: instead of scrolling, implement squeezing tabs into window width
        //  - maybe don't compute, sqeeze in paint and mouse processing

        auto & tabs = *reinterpret_cast <std::vector <Tab> *> (Get (hWnd, Index::Data));
        if (!tabs.empty ()) {
            if (auto hDC = GetDC (hWnd)) {
                auto dpiX = LOWORD (Get (hWnd, Index::DPI));
                auto dpiY = HIWORD (Get (hWnd, Index::DPI));
                auto hPrevious = SelectObject (hDC, (HFONT) Get (hWnd, Index::Font));
                auto minheight = 0;
                auto height = GetClientRect (hWnd).bottom;

                for (auto & tab : tabs) {
                    tab.left = true;
                    tab.right = true;
                    tab.r.top = 2 * dpiY / 96;
                    tab.r.bottom = height - 1 * dpiY / 96;
                }

                tabs [0].r.left = 2;

                const auto n = tabs.size ();
                for (auto i = 0u; i != n; ++i) {
                    RECT r = { 0,0,0,0 };
                    DrawTextEx (hDC, const_cast <LPTSTR> (tabs [i].text.c_str ()),
                                -1, &r, DT_CALCRECT | DT_SINGLELINE, NULL);

                    minheight = std::max (minheight, (int) r.bottom);

                    tabs [i].r.right = tabs [i].r.left + r.right + 12 * dpiX / 96;
                    if (tabs [i].close) {
                        tabs [i].r.right += height - dpiX * 4 / 96;

                        tabs [i].rCloseButton.top = tabs [i].r.top + dpiY * 2 / 96;
                        tabs [i].rCloseButton.left = tabs [i].r.right - tabs [i].r.bottom + dpiX * 4 / 96;
                        tabs [i].rCloseButton.right = tabs [i].r.right - dpiX * 2 / 96;
                        tabs [i].rCloseButton.bottom = tabs [i].r.bottom - dpiY * 2 / 96;
                    }
                    if (i + 1 != n) {
                        tabs [i + 1].r.left = tabs [i].r.right;
                    }
                }

                auto current = (ULONG_PTR) Get (hWnd, Index::Current);
                if (current < n) {
                    tabs [current].r.top = 0;
                    tabs [current].r.left -= 2;
                    tabs [current].r.right += 2;
                    tabs [current].r.bottom += 1 * dpiY / 96;

                    tabs [current].rCloseButton.top -= 2;
                    tabs [current].rCloseButton.left += 1;
                    tabs [current].rCloseButton.right += 1;
                    tabs [current].rCloseButton.bottom -= 2;

                    if (current > 0) {
                        tabs [current - 1].right = false;
                        tabs [current - 1].r.right -= 2;
                    }
                    if (current < n - 1) {
                        tabs [current + 1].left = false;
                        tabs [current + 1].r.left += 2;
                    }
                }

                if (hPrevious) {
                    SelectObject (hDC, hPrevious);
                }
                ReleaseDC (hWnd, hDC);

                if (auto first = Get (hWnd, Index::Offset)) {
                    auto offset = tabs [first].r.left - height;
                    if (first == current) {
                        offset += 2;
                    }
                    for (auto & tab : tabs) {
                        tab.r.left -= offset;
                        tab.r.right -= offset;
                        tab.rCloseButton.left -= offset;
                        tab.rCloseButton.right -= offset;
                    }
                }

                minheight += 11 * dpiY / 96;
                Set (hWnd, Index::MinHeight, minheight);

                InvalidateRect (hWnd, NULL, FALSE);
                return tabs.back ().r.right - tabs.front ().r.left;
            }
        }
        return 0;
    }

    bool IntersectRect (const RECT * r1, const RECT * r2) {
        RECT rTemp;
        return IntersectRect (&rTemp, r1, r2);
    }

    LRESULT OnPaint (HWND hWnd, HDC _hDC, RECT rcInvalidated) {
        RECT rc = GetClientRect (hWnd);
        HDC hDC = NULL;
        HANDLE hBuffered = NULL;
        HANDLE hTheme = (HTHEME) Get (hWnd, Index::Theme);
        HBITMAP hOffBitmap = NULL;
        HGDIOBJ hOffOld = NULL;

        if (ptrBeginBufferedPaint) {
            hBuffered = ptrBeginBufferedPaint (_hDC, &rc, BPBF_DIB, NULL, &hDC);
            if (hBuffered) {
                rcInvalidated = rc;
            }
        }

        if (!hBuffered) {
            hDC = CreateCompatibleDC (_hDC);
            if (hDC) {
                hOffBitmap = CreateCompatibleBitmap (_hDC, rc.right, rc.bottom);
                if (hOffBitmap) {
                    hOffOld = SelectObject (hDC, hOffBitmap);
                } else {
                    DeleteDC (hDC);
                }
            }
            if (!hOffBitmap) {
                hDC = _hDC;
            }
        }

        const auto hot = Get (hWnd, Index::Hot);
        const auto first = Get (hWnd, Index::Offset);
        const auto current = Get (hWnd, Index::Current);
        const auto pressed = Get (hWnd, Index::Pressed);

        const auto & tabs = *reinterpret_cast <std::vector <Tab> *> (Get (hWnd, Index::Data));
        const auto hPrevious = SelectObject (hDC, (HFONT) Get (hWnd, Index::Font));

        FillRect (hDC, &rc,
                  (HBRUSH) SendMessage (GetParent (hWnd), WM_CTLCOLORBTN,
                                        (WPARAM) hDC, (LPARAM) hWnd));

        if (hTheme == NULL) {
            RECT rLine = rc;
            rLine.top = rLine.bottom - 1;
            FillRect (hDC, &rLine, GetSysColorBrush (COLOR_3DDKSHADOW));
            --rLine.top; --rLine.bottom;
            FillRect (hDC, &rLine, GetSysColorBrush (COLOR_3DSHADOW));
        }

        if (!tabs.empty ()) {
            SetBkMode (hDC, TRANSPARENT);

            for (auto i = first ? first - 1 : 0; i != tabs.size (); ++i) {
                if (IntersectRect (&tabs [i].r, &rcInvalidated)) {

                    if (hTheme) {
                        int part = TABP_TABITEM;
                        int state = TIS_NORMAL;

                        if (i == 0) part += 1; // ...LEFTEDGE
                        if (i == current) part += 4; // ...TOP...

                        if (i == current) state = 3; // ...SELECTED
                        else if (i == hot) state = 2; // ...HOT

                        RECT r = tabs [i].r;
                        if (!tabs [i].left) {
                            r.left -= 2;
                        }
                        if (!tabs [i].right) {
                            r.right += 2;
                        }

                        // TODO: on XP blend partial tabs

                        r.bottom += 1;
                        DrawThemeBackground (hTheme, hDC, part, state, &r, &tabs [i].r);

                        if (tabs [i].close && (i >= first)) {
                            auto hThemeButton = (HTHEME) Get (hWnd, Index::WndTheme);

                            r = tabs [i].rCloseButton;
                            if (!IsWindowsVistaOrGreater ()) {
                                r.top += 1;
                            }
                            if (i == pressed) {
                                DrawThemeBackground (hThemeButton, hDC, WP_SMALLCLOSEBUTTON, MDCL_PUSHED, &r, &tabs [i].r);
                            } else
                            if (hot == (0x8000'0000 | i)) {
                                DrawThemeBackground (hThemeButton, hDC, WP_SMALLCLOSEBUTTON, MDCL_HOT, &r, &tabs [i].r);
                            } else {
                                // DrawThemeBackground (hThemeButton, hDC, WP_SMALLCLOSEBUTTON, MDCL_DISABLED, &r, &tabs [i].r);
                                DrawThemeBackground (hThemeButton, hDC, WP_MDICLOSEBUTTON, MDCL_NORMAL, &r, &tabs [i].r);
                            }
                        }
                        
                    } else {
                        RECT rEdge = tabs [i].r;
                        rEdge.bottom += 1;

                        RECT rFill = tabs [i].r;
                        rFill.top += 2;

                        UINT edge = BF_TOP;

                        if (tabs [i].left) {
                            edge |= BF_LEFT;
                            rFill.left += 2;
                        }
                        if (tabs [i].right) {
                            edge |= BF_RIGHT;
                            rFill.right -= 2;
                        }
                        if (i != current) {
                            rEdge.bottom -= 2;
                            rFill.bottom -= 1;
                        }

                        FillRect (hDC, &rFill, GetSysColorBrush ((i == current) ? COLOR_WINDOW : COLOR_BTNFACE));
                        DrawEdge (hDC, &rEdge, BDR_RAISED, edge);

                        if (tabs [i].close) {
                            RECT r = tabs [i].rCloseButton;
                            r.top += 1;
                            r.right -= 1;

                            if (i != current) {
                                DrawEdge (hDC, &r, BDR_SUNKENOUTER, BF_RECT);
                            }

                            auto style = DFCS_CAPTIONCLOSE;

                            if (i == pressed) style |= DFCS_PUSHED;
                            if (hot == (0x8000'0000 | i)) style |= DFCS_HOT;
                            if (i == current && i != pressed) style |= DFCS_FLAT;

                            InflateRect (&r, -1, -1);
                            DrawFrameControl (hDC, &r, DFC_CAPTION, style);
                        }
                    }

                    RECT rText = tabs [i].r;
                    rText.left -= 1;

                    if (GetForegroundWindow () == GetParent (hWnd)) {
                        SetTextColor (hDC, GetSysColor (COLOR_BTNTEXT));
                    } else {
                        SetTextColor (hDC, GetSysColor (COLOR_GRAYTEXT));
                    }

                    if (!hTheme) {
                        if (i == hot && i != current) {
                            SetTextColor (hDC, GetSysColor (COLOR_HOTLIGHT));
                        }
                    }
                    if (tabs [i].close) {
                        rText.right -= rc.bottom - 4;
                    }

                    rText.top += 4;
                    if (!tabs [i].left) rText.right -= 2u;
                    if (!tabs [i].right) rText.left += 2u;

                    DrawTextEx (hDC, const_cast <LPTSTR> (tabs [i].text.c_str ()),
                                -1, &rText, DT_NOCLIP | DT_TOP | DT_SINGLELINE | DT_CENTER, NULL);

                    if (ptrBufferedPaintSetAlpha) {
                            // TODO: alpha 64 and premultiply bits for partial tabs
                        /*if (i < first) {
                            ptrBufferedPaintSetAlpha (hBuffered, &tabs [i].r, 0);
                        } else*/ {
                            ptrBufferedPaintSetAlpha (hBuffered, &tabs [i].r, 255);
                        }
                    }
                }
            }

            if ((GetFocus () == hWnd) && (current >= first)) {
                RECT rFocus = tabs [current].r;

                rFocus.left += 2;
                rFocus.top += 2;
                rFocus.right -= 2;
                rFocus.bottom -= 1;

                if (!hTheme) {
                    rFocus.right -= 1;
                }
                if (!(LOWORD (SendMessage (GetParent (hWnd), WM_QUERYUISTATE, 0, 0)) & UISF_HIDEFOCUS)) {
                    DrawFocusRect (hDC, &rFocus);
                }
            }
        }

        if (hBuffered) {
            if (tabs.back ().r.right > rc.right) {
                // TODO: fade out + button
            }

            ptrEndBufferedPaint (hBuffered, TRUE);
        } else
        if (hOffBitmap) {
            BitBlt (_hDC,
                    rcInvalidated.left, rcInvalidated.top,
                    rcInvalidated.right - rcInvalidated.left,
                    rcInvalidated.bottom - rcInvalidated.top,
                    hDC,
                    rcInvalidated.left, rcInvalidated.top,
                    SRCCOPY);

            SelectObject (hDC, hOffOld);
            if (hOffBitmap) {
                DeleteObject (hOffBitmap);
            }
            if (hDC) {
                DeleteDC (hDC);
            }
        }
        return 0;
    }

    LRESULT OnHitTest (HWND hWnd, SHORT x, SHORT y) {
        POINT pt = { x, y };

        if (ScreenToClient (hWnd, &pt)) {
            for (auto & tab : *reinterpret_cast <const std::vector <Tab> *> (Get (hWnd, Index::Data)))
                if (PtInRect (&tab.r, pt))
                    return HTCLIENT;
        }
        return HTTRANSPARENT;
    };

    LRESULT OnClick (HWND hWnd, UINT message, SHORT x, SHORT y) {
        POINT pt = { x, y };

        auto offset = (ULONG_PTR) Get (hWnd, Index::Offset);
        const auto & tabs = *reinterpret_cast <const std::vector <Tab> *> (Get (hWnd, Index::Data));

        for (auto i = offset; i != tabs.size (); ++i) {
            if (PtInRect (&tabs [i].r, pt)) {
                
                NMMOUSE nm = {
                    { hWnd, (UINT) GetDlgCtrlID (hWnd), 0 },
                    i, (DWORD_PTR) tabs [i].id, pt, HTCLIENT
                };
                switch (message) {
                    case WM_RBUTTONDOWN: nm.hdr.code = NM_RDOWN; break;
                    case WM_LBUTTONDBLCLK: nm.hdr.code = NM_DBLCLK; break;
                    case WM_RBUTTONDBLCLK: nm.hdr.code = NM_RDBLCLK; break;
                }

                if (PtInRect (&tabs [i].rCloseButton, pt)) {
                    nm.dwHitInfo = HTCLOSE;

                    if (message == WM_LBUTTONUP) {
                        nm.hdr.code = NM_CLICK;
                    }
                } else {
                    if (message == WM_LBUTTONDOWN) {
                        nm.hdr.code = NM_CLICK;
                    }
                }

                if (nm.hdr.code) {
                    MapWindowPoints (hWnd, NULL, &nm.pt, 1u);

                    if (!SendMessage (GetParent (hWnd), WM_NOTIFY, nm.hdr.idFrom, (LPARAM) &nm)) {

                        if (message == WM_LBUTTONDOWN) { // tab clicked, not close button
                            if (Get (hWnd, Index::Current) != i) {
                                Set (hWnd, Index::Current, i);

                                UpdatePresentation (hWnd);
                                
                                auto rc = GetClientRect (hWnd);
                                while (tabs [i].r.right > rc.right) {
                                    Right (hWnd, &tabs, offset, &rc);
                                }

                                InvalidateRect (hWnd, NULL, FALSE);
                                SendMessage (GetParent (hWnd), WM_COMMAND,
                                             MAKEWPARAM (GetDlgCtrlID (hWnd), i), (LPARAM) hWnd);
                            }
                        }
                    }
                }

                if (nm.dwHitInfo == HTCLOSE) {
                    switch (message) {
                        case WM_LBUTTONDOWN:
                            Set (hWnd, Index::Pressed, i);
                            break;
                        case WM_LBUTTONUP:
                            Set (hWnd, Index::Pressed, -1);
                            break;
                    }
                    UpdatePresentation (hWnd);
                    InvalidateRect (hWnd, NULL, FALSE);
                }
                break;
            }
        }
        return 0;
    }

    LRESULT OnMouseMove (HWND hWnd, SHORT x, SHORT y) {
        POINT pt = { x, y };

        auto hot = -1;
        auto was = -1;

        const auto & tabs = *reinterpret_cast <const std::vector <Tab> *> (Get (hWnd, Index::Data));
        for (auto i = 0u; i != tabs.size (); ++i) {
            if (PtInRect (&tabs[i].r, pt)) {
                hot = i;
                if (PtInRect (&tabs [i].rCloseButton, pt)) {
                    hot |= 0x8000'0000;
                }
                break;
            }
        }

        was = (int) Set (hWnd, Index::Hot, hot);

        if (was != hot) {
            if (hot != -1) {
                InvalidateRect (hWnd, &tabs [hot & ~0x8000'0000].r, FALSE);
            }
            if (was != -1) {
                InvalidateRect (hWnd, &tabs [was & ~0x8000'0000].r, FALSE);
            }
        }

        if ((x == -1) && (y == -1)) {
            Set (hWnd, Index::Pressed, -1);
        } else {
            TRACKMOUSEEVENT tme = {
                sizeof (TRACKMOUSEEVENT),
                TME_LEAVE, hWnd, HOVER_DEFAULT
            };
            TrackMouseEvent (&tme);
        }
        return 0;
    }

    LRESULT OnKeyDown (HWND hWnd, WPARAM wParam, LPARAM lParam) {
        NMKEY nmKey = {
            { hWnd, (UINT) GetDlgCtrlID (hWnd), (UINT) NM_KEYDOWN },
            (UINT) wParam, (UINT) lParam
        };
        if (!SendMessage (GetParent (hWnd), WM_NOTIFY, nmKey.hdr.idFrom, (LPARAM) &nmKey)) {
            switch (wParam) {
                case VK_LEFT:
                    if (GetKeyState (VK_CONTROL) & 0x8000) {
                        Left (hWnd);
                    } else {
                        Previous (hWnd);
                    }
                    break;
                case VK_RIGHT:
                    if (GetKeyState (VK_CONTROL) & 0x8000) {
                        Right (hWnd);
                    } else {
                        Next (hWnd);
                    }
                    break;
            }
        }
        return 0;
    }

    void Next (HWND hWnd) {
        auto rc = GetClientRect (hWnd);
        auto current = (ULONG_PTR) Get (hWnd, Index::Current);
        auto offset = (ULONG_PTR) Get (hWnd, Index::Offset);
        auto & tabs = *reinterpret_cast <const std::vector <Tab> *> (Get (hWnd, Index::Data));

        current++;

        if (current < tabs.size ()) {
            while (tabs [current].r.right > rc.right) {
                Right (hWnd, &tabs, offset, &rc);
            }
            Set (hWnd, Index::Current, current);
            UpdatePresentation (hWnd);
        }
    }
    void Previous (HWND hWnd) {
        auto current = Get (hWnd, Index::Current);
        if (current > 0) {
            current--;
            Set (hWnd, Index::Current, current);

            auto offset = Get (hWnd, Index::Offset);
            if (current < offset) {
                Set (hWnd, Index::Offset, current);
            }
            UpdatePresentation (hWnd);
        }
    }

    void Left (HWND hWnd) {
        if (auto offset = Get (hWnd, Index::Offset)) {
            offset -= 1;

            Set (hWnd, Index::Offset, offset);
            UpdatePresentation (hWnd);
        }
    }
    void Right (HWND hWnd) {
        auto offset = (ULONG_PTR) Get (hWnd, Index::Offset);
        auto tabs = reinterpret_cast <std::vector <Tab> *> (Get (hWnd, Index::Data));
        auto rc = GetClientRect (hWnd);

        Right (hWnd, tabs, offset, &rc);
    }

    void Right (HWND hWnd, const std::vector <Tab> * tabs, ULONG_PTR & offset, const RECT * rc) {
        offset += 1;
        if ((offset < tabs->size ()) && (tabs->back ().r.right > rc->right)) {
            Set (hWnd, Index::Offset, offset);
            UpdatePresentation (hWnd);
        }
    }

    LRESULT OnHorizontalWheel (HWND hWnd, LONG distance, USHORT flags) {
        distance += (LONG) Get (hWnd, Index::WheelDelta);

        while (distance >= +WHEEL_DELTA) {
            distance -= WHEEL_DELTA;
            Right (hWnd);
        }
        while (distance <= -WHEEL_DELTA) {
            distance += WHEEL_DELTA;
            Left (hWnd);
        }

        Set (hWnd, Index::WheelDelta, distance);
        return 0;
    }
}
