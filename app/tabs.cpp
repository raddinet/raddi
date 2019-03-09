#include "tabs.h"
#include "appapi.h"
// #include "../common/log.h"
#include <VersionHelpers.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <vssym32.h>
#include <algorithm>

#include <string>
#include <vector>
#include <map>

namespace {
    LRESULT CALLBACK Procedure (HWND, UINT, WPARAM, LPARAM);

    struct ThemeHandle {
        HTHEME handle = NULL;

        void update (HWND hWnd, LPCWSTR name) {
            if (this->handle) {
                CloseThemeData (this->handle);
            }
            if (hWnd) {
                this->handle = OpenThemeData (hWnd, name);
            }
        }
        operator HTHEME () const {
            return this->handle;
        }
        ~ThemeHandle () {
            this->update (NULL, NULL);
        }
    };

    struct StackState {
		struct TabRef {
			std::size_t id;
			RECT		tag = { 0,0,0,0 };
		};

        std::vector <TabRef> tabs;
        std::size_t     top; // ID of top tab
        std::size_t     badge;
        std::uint8_t    progress;
        std::uint8_t    left : 1;
        std::uint8_t    right : 1;
        std::uint8_t    locked : 1;
        std::uint16_t   min_width;
        std::uint16_t   max_width;
        std::uint16_t   ideal_width;
        RECT            r;
        RECT            rContent;
        RECT            rCloseButton;
    };

    struct TabControlState : TabControlInterface {
        HWND hWnd;
        HFONT font;
        ThemeHandle theme;
        ThemeHandle window;

        std::vector <StackState> stacks;
        std::size_t current = 0; // current stack

		struct Hot {
			std::size_t stack = -1; // index
			std::size_t tab = 0; // tab ID

			bool tag = false;
			bool close = false;
			bool down = false;

			bool operator != (const Hot & other) const {
				return this->stack != other.stack
					|| this->tab != other.tab
					|| this->tag != other.tag
					|| this->close != other.close
					|| this->down != other.down
					;
			}
		} hot;

        short wheel_delta = 0;
        
        explicit TabControlState (HWND hWnd) : hWnd (hWnd) {
            this->stacks.reserve (32);
        }
        void update_themes () {
            this->theme.update (this->hWnd, VSCLASS_TAB);
            this->window.update (this->hWnd, VSCLASS_WINDOW);
        }
        void update () override;
        void stack (std::size_t which, std::size_t into, bool after) override;
        
        void repaint (HDC, RECT rc);
        UINT hittest (POINT);
        UINT mouse (POINT);
        UINT click (UINT, POINT);

    private:
        void update_stacks_state ();
        void update_visual_representation ();
        StackState * get_tab_stack (std::size_t tab, std::vector <StackState::TabRef> ::iterator * = nullptr);
    };

    TabControlState * state (HWND hWnd) {
        return reinterpret_cast <TabControlState *> (GetWindowLongPtr (hWnd, GWLP_USERDATA));
    }

    RECT GetClientRect (HWND hWnd) {
        RECT r;
        if (!GetClientRect (hWnd, &r)) {
            std::memset (&r, 0, sizeof r);
        }
        return r;
    }
    SIZE GetClientSize (HWND hWnd) {
        auto r = GetClientRect (hWnd);
        return { r.right, r.bottom };
    }
}

ATOM InitializeTabControl (HINSTANCE hInstance) {
    WNDCLASSEX wc = {
        sizeof (WNDCLASSEX), CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW,
        Procedure, 0, 0, hInstance,  NULL,
        LoadCursor (NULL, IDC_ARROW), NULL, NULL, L"RADDI:Tabs", NULL
    };
    return RegisterClassEx (&wc);
}

HWND CreateTabControl (HINSTANCE hInstance, HWND hParent, UINT style, UINT id) {
    return CreateWindowEx (WS_EX_NOPARENTNOTIFY, TEXT ("RADDI:Tabs"), TEXT (""),
                           style | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                           0,0,0,0, hParent, (HMENU) (std::intptr_t) id, hInstance, NULL);
}

namespace {
    LRESULT CALLBACK Procedure (HWND hWnd, UINT message,
                                WPARAM wParam, LPARAM lParam) {
        try {
            switch (message) {
                case WM_NCCREATE:
                    try {
                        SetWindowLongPtr (hWnd, GWLP_USERDATA, (LONG_PTR) new TabControlState (hWnd));
                        return TRUE;
                    } catch (const std::bad_alloc &) {
                        return FALSE;
                    }
                case WM_CREATE:
                    state (hWnd)->update_themes ();
                    SendMessage (hWnd, WM_CHANGEUISTATE, UIS_INITIALIZE, 0u);
                    return 0;
                case WM_DESTROY:
                    delete state (hWnd);
                    return 0;

                case WM_SETFONT:
                    state (hWnd)->font = (HFONT) wParam;
                    if (lParam & 1) {
                        InvalidateRect (hWnd, NULL, FALSE);
                    }
                    break;
                case WM_GETFONT:
                    return (LRESULT) state (hWnd)->font;

                case WM_SIZE:
                    state (hWnd)->update ();
                    return 0;

                case WM_PAINT: {
                    PAINTSTRUCT ps;
                    if (HDC hDC = BeginPaint (hWnd, &ps)) {
                        state (hWnd)->repaint (hDC, ps.rcPaint);
                    }
                    EndPaint (hWnd, &ps);
                } break;

                case WM_PRINTCLIENT:
                    state (hWnd)->repaint ((HDC) wParam, GetClientRect (hWnd));
                    return 0;

                case WM_NCHITTEST:
                    return state (hWnd)->hittest ({ (short) LOWORD (lParam), (short) HIWORD (lParam) });
                case WM_MOUSEMOVE:
                    return state (hWnd)->mouse ({ (short) LOWORD (lParam), (short) HIWORD (lParam) });
                case WM_MOUSELEAVE:
                    return state (hWnd)->mouse ({ -1, -1 });

                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                case WM_RBUTTONUP:
                case WM_RBUTTONDOWN:
                case WM_RBUTTONDBLCLK:
                    return state (hWnd)->click (message, { (short) LOWORD (lParam), (short) HIWORD (lParam) });
                    /*
                case WM_KEYDOWN:
                    return OnKeyDown (hWnd, wParam, lParam);
                    
                case WM_MOUSEHWHEEL:
                    return OnHorizontalWheel (hWnd, (LONG) (SHORT) HIWORD (wParam), LOWORD (wParam));
                    */
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
                    state (hWnd)->update_themes ();
                    InvalidateRect (hWnd, NULL, FALSE);
                    break;

                case WM_GETDLGCODE:
                    switch (wParam) {
                        case VK_LEFT:
                        case VK_RIGHT:
                            return DLGC_WANTARROWS;
                    }
                    break;
            }
            return DefWindowProc (hWnd, message, wParam, lParam);

        } catch (const std::bad_alloc &) {
            NMHDR nm = { hWnd, (UINT) GetDlgCtrlID (hWnd), (UINT) NM_OUTOFMEMORY };
            return SendMessage (GetParent (hWnd), WM_NOTIFY, nm.idFrom, (LPARAM) &nm);

        } catch (const std::out_of_range &) {
            return 0;
        } catch (const std::exception &) {
            return 0;
        }
    }
}

StackState * TabControlState::get_tab_stack (std::size_t tab, std::vector <StackState::TabRef> ::iterator * ii) {
    for (auto & stack : this->stacks) {
        
        auto i = stack.tabs.begin ();
        auto e = stack.tabs.end ();
        for (; i != e; ++i) {
            if (i->id == tab) {
                if (ii) {
                    *ii = i;
                }
                return &stack;
            }
        }
    }
    return nullptr;
}

void TabControlState::stack (std::size_t which, std::size_t with, bool after) {
    this->update_stacks_state ();

    std::vector <StackState::TabRef> ::iterator from_ii;
    std::vector <StackState::TabRef> ::iterator into_ii;
    auto from = this->get_tab_stack (which, &from_ii);
    auto into = this->get_tab_stack (with, &into_ii);

    if (from && into) {
        if (into->tabs.size () == 1) {
            into->tabs.reserve (8);
        }
        if (after) {
            into->tabs.insert (++into_ii, *from_ii);
        } else {
            into->tabs.insert (into->tabs.end (), *from_ii);
        }
        from->tabs.erase (from_ii);
    }
}

void TabControlState::update_stacks_state () {
    // create stacks for new tabs
    for (const auto & tab : this->tabs) {
        const auto id = tab.first;
        if (this->get_tab_stack (id) == nullptr) {
            this->stacks.push_back (StackState ());

            // this->stacks.back ().tabs.reserve ()
			this->stacks.back ().tabs.push_back ({ id });
            this->stacks.back ().top = id;
        }
    }

    // clear stacks of removed tabs
    auto i = this->stacks.begin ();
    auto e = this->stacks.end ();

    while (i != e) {
        auto ti = i->tabs.begin ();
        auto te = i->tabs.end ();

        while (ti != te) {
            if (!this->tabs.count (ti->id)) {
                bool top = (i->top == ti->id);

                ti = i->tabs.erase (ti);
                te = i->tabs.end ();
                if (top) {
                    if (ti == te) {
                        if (!i->tabs.empty ()) {
                            i->top = i->tabs.back ().id;
                        }
                    } else {
                        i->top = ti->id;
                    }
                }
            } else {
                ++ti;
            }
        }
        // remove stack if empty
        if (i->tabs.empty ()) {
            i = this->stacks.erase (i);
            e = this->stacks.end ();

			std::size_t index = i - this->stacks.begin ();

			if (index == this->current) {
				if ((i == e) && (index > 0)) {
					--this->current;
				}
				if (this->current < this->stacks.size ()) {
					if (auto h = this->tabs [this->stacks [this->current].top].content) {
						ShowWindow (h, SW_SHOW);
					}
				}
			} else
			if (index < this->current) {
				--this->current;
			}
		} else {
			++i;
        }
    }

    // combine tabs info for stacks
    for (auto & stack : this->stacks) {
        stack.min_width = 0;
		stack.max_width = 65535;
        stack.badge = 0;
        stack.progress = 0;

        for (auto tabref : stack.tabs) {
            const auto & tab = this->tabs [tabref.id];

            if (tab.max_width) {
                stack.max_width = std::min (stack.max_width, tab.max_width);
            }
            stack.min_width = std::max (stack.min_width, tab.min_width);
            stack.badge += tab.badge;

            if (tab.progress != 0) {
                if (stack.progress) {
                    stack.progress = std::min (stack.progress, tab.progress);
                } else {
                    stack.progress = tab.progress;
                }
            }
        }
    }
}

void TabControlState::update () {
    this->update_stacks_state ();
    this->update_visual_representation ();
    InvalidateRect (hWnd, NULL, FALSE);
}

void TabControlState::update_visual_representation () {
    this->overflow.clear ();

    const auto size = GetClientSize (hWnd);
    for (auto & stack : this->stacks) {
        stack.left = true;
        stack.right = true;
        stack.locked = false;
        stack.r.top = 2 * dpi.y / 96;
        stack.r.bottom = size.cy - 1 * dpi.x / 96;
    }
    if (!this->stacks.empty ()) {
        this->stacks.front ().r.left = 2 * dpi.x / 96;
    }

    if (auto hDC = GetDC (hWnd)) {
        auto hPreviousFont = SelectObject (hDC, this->font);
        
        this->minimum.cy = 0;
        this->minimum.cx = 2 * dpi.x / 96;

		auto ideal_width = 6 * dpi.x / 96;
		std::size_t locked_stacks = 0;

        for (auto & stack : this->stacks) {
			if (this->tabs [stack.top].fit || (this->max_tab_width == 0)) {
				RECT r = { 0,0,0,0 };
				DrawTextEx (hDC, const_cast <LPTSTR> (this->tabs [stack.top].text.c_str ()),
							-1, &r, DT_CALCRECT | DT_SINGLELINE, NULL);

				stack.ideal_width = (std::uint16_t) (r.right + 12 * dpi.x / 96);
				this->minimum.cy = std::max (this->minimum.cy, r.bottom);
			} else {
				stack.ideal_width = (std::uint16_t) (this->max_tab_width * dpi.x / 96);
			}
            if (this->badges && this->tabs [stack.top].fit) {
                stack.ideal_width += (std::uint16_t) (10 * dpi.x / 96);
            }

            if (stack.ideal_width > stack.max_width) stack.ideal_width = stack.max_width;
			if (stack.ideal_width < stack.min_width) stack.ideal_width = stack.min_width;
            if (stack.ideal_width < this->min_tab_width) stack.ideal_width = this->min_tab_width;

			if (this->tabs [stack.top].locked) {
                stack.locked = true;
				++locked_stacks;
			}
			ideal_width += stack.ideal_width;
		}

		bool tight = (ideal_width > size.cx);
        long reduce = 0;

        if (tight) {
            while (auto flexibility = this->stacks.size () - locked_stacks) {
                reduce = (ideal_width - size.cx) / flexibility;

                bool any = false;
                for (auto & stack : this->stacks) {
                    if (!stack.locked) {
                        if (stack.ideal_width - reduce < long (stack.min_width)) {
                            
                            ideal_width -= stack.ideal_width;
                            ideal_width += stack.min_width;
                            stack.ideal_width = stack.min_width;
                            stack.locked = true;

                            ++locked_stacks;
                            any = true;
                            break;
                        }
                    }
                }

                if (!any)
                    break;
            }
        }

        for (auto & stack : this->stacks) {
			long width = stack.ideal_width;

			if (!stack.locked) {
				if (width >= reduce) {
					width -= reduce;
				}
			}

            stack.r.left = this->minimum.cx;
            stack.r.right = stack.r.left + width;

            stack.rContent = stack.r;
            stack.rContent.top += 4 * dpi.y / 96;
            stack.rContent.left += 5 * dpi.x/ 96;
            stack.rContent.right -= 4 * dpi.x / 96;

            if (this->tabs [stack.top].close) {
                stack.rContent.right = stack.rCloseButton.left;
                stack.rCloseButton.top = stack.r.top + dpi.y * 4 / 96;
                stack.rCloseButton.left = stack.r.right - stack.r.bottom + dpi.x * 6 / 96;
                stack.rCloseButton.right = stack.r.right - dpi.x * 4 / 96;
                stack.rCloseButton.bottom = stack.r.bottom - dpi.y * 4 / 96;
            }

            this->minimum.cx = stack.r.right;

			for (std::size_t i = 0, n = stack.tabs.size (); i != n; ++i) {
				stack.tabs [i].tag.top = stack.r.top;
				stack.tabs [i].tag.bottom = stack.r.top + 5 * dpi.y / 96;
				stack.tabs [i].tag.left = stack.r.left + (1 * dpi.x / 96) + (LONG (i) + 0) * (stack.r.right - stack.r.left - (2 * dpi.x / 96)) / n;
				stack.tabs [i].tag.right = stack.r.left + (1 * dpi.x / 96) + (LONG (i) + 1) * (stack.r.right - stack.r.left - (2 * dpi.x / 96)) / n;

				if (stack.top == stack.tabs [i].id) { // current tab in stack
					stack.tabs [i].tag.bottom = stack.tabs [i].tag.top;
				}
                if (this->minimum.cx > size.cx) {
                    this->overflow.push_back (stack.tabs [i].id);
                }
			}
        }

		if (this->minimum.cy == 0) {
            RECT r = { 0,0,0,0 };
            DrawTextEx (hDC, const_cast <LPTSTR> (L"y"), 1, &r, DT_CALCRECT | DT_SINGLELINE, NULL);
            this->minimum.cy = r.bottom;
		}

        if (this->current < this->stacks.size ()) {
            auto stack = &this->stacks [this->current];
            stack->r.top = 0;
            stack->r.left -= 2 * dpi.x / 96;
            stack->r.right += 2 * dpi.x / 96;
            stack->r.bottom = size.cy;
            stack->rContent.top -= 2 * dpi.x / 96;
            stack->rContent.bottom -= 1 * dpi.y / 96;
            stack->rCloseButton.top -= 2;
            stack->rCloseButton.bottom -= 2;

            if (this->current > 0) {
                this->stacks [this->current - 1].right = false;
                this->stacks [this->current - 1].r.right -= 2 * dpi.x / 96;
            }
            if (this->current < this->stacks.size () - 1) {
                this->stacks [this->current + 1].left = false;
                this->stacks [this->current + 1].r.left += 2 * dpi.x / 96;
            }

			for (std::size_t i = 0; i != stack->tabs.size (); ++i) {
				stack->tabs [i].tag.bottom -= stack->tabs [i].tag.top;
				stack->tabs [i].tag.top = 0;
			}
        }

        if (hPreviousFont) {
            SelectObject (hDC, hPreviousFont);
        }
        ReleaseDC (hWnd, hDC);
    }

    this->minimum.cy += 11 * dpi.y / 96;
}

UINT TabControlState::hittest (POINT pt) {
    if (ScreenToClient (hWnd, &pt)) {
        for (const auto & stack : this->stacks)
            if (PtInRect (&stack.r, pt))
                return HTCLIENT;
    }
    return HTTRANSPARENT;
};

namespace {
    bool IntersectRect (const RECT * r1, const RECT * r2) {
        RECT rTemp;
        return IntersectRect (&rTemp, r1, r2);
    }
}

void TabControlState::repaint (HDC _hDC, RECT rcInvalidated) {
    this->update_stacks_state ();
    this->update_visual_representation ();

    RECT rc = GetClientRect (hWnd);
    HDC hDC = NULL;
    HANDLE hBuffered = NULL;
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

    // fill with background

    auto hPreviousFont = SelectObject (hDC, this->font);

    FillRect (hDC, &rc, (HBRUSH) SendMessage (GetParent (hWnd), WM_CTLCOLORBTN, (WPARAM) hDC, (LPARAM) hWnd));
    SetBkMode (hDC, TRANSPARENT);

    // line on classic

    if (this->theme == NULL) {
        RECT rLine = rc;
        rLine.top = rLine.bottom - 1;
        FillRect (hDC, &rLine, GetSysColorBrush (COLOR_3DDKSHADOW));
        --rLine.top; --rLine.bottom;
        FillRect (hDC, &rLine, GetSysColorBrush (COLOR_3DSHADOW));
    }

    // paint actual tabs

    for (std::size_t i = 0u; i != this->stacks.size (); ++i) {
        const auto & stack = this->stacks [i];

        if (IntersectRect (&stack.r, &rcInvalidated)) {

            // TODO: add simple design paint (dark, light)
            if (this->theme) {
                int part = TABP_TABITEM;
                int state = TIS_NORMAL;

                if (i == 0) part += 1; // ...LEFTEDGE
                if (i == this->current) part += 4; // ...TOP...

                if (i == this->current) state = 3; // ...SELECTED
                else if (i == this->hot.stack) state = 2; // ...HOT

                RECT r = stack.r;
                if (!stack.left) r.left -= 2;
                if (!stack.right) r.right += 2;

                r.bottom += 1;
                DrawThemeBackground (this->theme, hDC, part, state, &r, &stack.r);

                if (this->tabs [stack.top].close) {
                    r = stack.rCloseButton;
                    if (!IsWindowsVistaOrGreater ()) {
                        r.top += 1;
                    }
                    if (i == this->hot.stack) {
                        if (this->hot.close && this->hot.down) {
                            DrawThemeBackground (this->window, hDC, WP_SMALLCLOSEBUTTON, MDCL_PUSHED, &r, &stack.r);
                        } else
                        if (this->hot.close) {
                            DrawThemeBackground (this->window, hDC, WP_SMALLCLOSEBUTTON, MDCL_HOT, &r, &stack.r);
                        } else {
                            DrawThemeBackground (this->window, hDC, WP_MDICLOSEBUTTON, MDCL_NORMAL, &r, &stack.r);
                        }
                    } else {
                        // DrawThemeBackground (this->window, hDC, WP_SMALLCLOSEBUTTON, MDCL_DISABLED, &r, &tabs [i].r);
                        DrawThemeBackground (this->window, hDC, WP_MDICLOSEBUTTON, MDCL_NORMAL, &r, &stack.r);
                    }
                }

            } else {
                RECT rEdge = stack.r; rEdge.bottom += 1;
                RECT rFill = stack.r; rFill.top += 2;
                UINT edge = BF_TOP;

                if (stack.left) { edge |= BF_LEFT; rFill.left += 2; }
                if (stack.right) { edge |= BF_RIGHT; rFill.right -= 2; }

                if (i != this->current) {
                    rEdge.bottom -= 2;
                    rFill.bottom -= 1;
                }

                FillRect (hDC, &rFill, GetSysColorBrush ((i == this->current) ? COLOR_WINDOW : COLOR_BTNFACE));
                DrawEdge (hDC, &rEdge, BDR_RAISED, edge);

                if (this->tabs [stack.top].close) {
                    RECT r = stack.rCloseButton;
                    r.top += 1;
                    r.right -= 1;

                    auto style = DFCS_CAPTIONCLOSE;

                    if (i != this->current) {
                        DrawEdge (hDC, &r, BDR_SUNKENOUTER, BF_RECT);
                    }
                    if (i == this->hot.stack) {
                        if (this->hot.close && this->hot.down) {
                            style |= DFCS_PUSHED;
                        } else
                        if (this->hot.close) {
                            style |= DFCS_HOT;
                        }
                    } else {
                        
                    }
                    if (i == this->current) {
                        if (i != this->hot.stack) {
                            style |= DFCS_FLAT;
                        }
                    }

                    InflateRect (&r, -1, -1);
                    DrawFrameControl (hDC, &r, DFC_CAPTION, style);
                }
            }

            if (stack.progress) {
                RECT rProgress = stack.r;
                InflateRect (&rProgress, -1 * dpi.x / 96, -1 * dpi.y / 96);
                rProgress.left += stack.progress * (rProgress.right - rProgress.left) / 255;

                SetDCBrushColor (hDC, 0xFFDDCC);
                FillRect (hDC, &rProgress, (HBRUSH) GetStockObject (DC_BRUSH));
            }

            // stack

            for (const auto & tabref : stack.tabs) {
				auto r = tabref.tag;
                if (r.bottom != r.top) {
                    r.left += 1 * dpi.x / 96;
                    r.right -= 1 * dpi.x / 96;
                    r.bottom -= 1 * dpi.y / 96;
                    
					if (this->hot.tag && (tabref.id == this->hot.tab)) {
						if (this->hot.down) {
							FillRect (hDC, &r, (HBRUSH) GetStockObject (BLACK_BRUSH));
						} else {
							FillRect (hDC, &r, GetSysColorBrush (COLOR_HOTLIGHT));
						}
					} else {
						FillRect (hDC, &r, GetSysColorBrush (COLOR_3DSHADOW));
					}
                }
            }

            // text & icon

            if (GetForegroundWindow () == GetParent (hWnd)) {
                SetTextColor (hDC, GetSysColor (COLOR_BTNTEXT));
            } else {
                SetTextColor (hDC, GetSysColor (COLOR_GRAYTEXT));
            }

            if (!this->theme) {
                if (i == this->hot.stack && i != this->current) {
                    SetTextColor (hDC, GetSysColor (COLOR_HOTLIGHT));
                }
            }

            if (auto f = this->tabs [stack.top].font) {
                SelectObject (hDC, f);
            } else {
                SelectObject (hDC, this->font);
            }

			RECT rText = stack.rContent;
            if (auto icon = this->tabs [stack.top].icon) {
                DrawIconEx (hDC, rText.left, rText.top, icon, 0, 0, 0, NULL, DI_NORMAL);

                ICONINFO ii;
                if (GetIconInfo (icon, &ii)) {
                    BITMAP bmi;
                    if (GetObject (ii.hbmColor, sizeof bmi, &bmi)) {
                        rText.left += bmi.bmWidth + 2 * dpi.x / 96;
                    }

                    DeleteObject (ii.hbmColor);
                    DeleteObject (ii.hbmMask);
                }
            }

            DrawTextEx (hDC, const_cast <LPTSTR> (this->tabs [stack.top].text.c_str ()),
                        -1, &rText, DT_TOP | DT_LEFT | DT_NOCLIP | DT_SINGLELINE | DT_END_ELLIPSIS, NULL);

            // badge

            if (stack.badge) {
                wchar_t badge [4];
                if (stack.badge < 100) {
                    std::swprintf (badge, 4, L"%u", stack.badge);
                } else {
                    std::swprintf (badge, 4, L"##");
                }

                RECT r = stack.rContent;
                SetTextColor (hDC, 0x0000FF);
                SelectObject (hDC, this->font);
                DrawTextEx (hDC, badge, -1, &r, DT_BOTTOM | DT_RIGHT | DT_SINGLELINE, NULL);
            }

            if (ptrBufferedPaintSetAlpha) {
                ptrBufferedPaintSetAlpha (hBuffered, &stack.r, 255);
            }
        }
    }

    if (GetFocus () == hWnd) {
        if (!(LOWORD (SendMessage (GetParent (hWnd), WM_QUERYUISTATE, 0, 0)) & UISF_HIDEFOCUS)) {
			if (this->current < this->stacks.size ()) {
				auto r = this->stacks [this->current].r;
				InflateRect (&r, -2, -2);

				if (!this->theme) {
					--r.right;
				}
				DrawFocusRect (hDC, &r);
			}
        }
    }

    // finish and swap to screen

    if (hPreviousFont) {
        SelectObject (hDC, hPreviousFont);
    }

    if (hBuffered) {
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
}

UINT TabControlState::click (UINT message, POINT pt) {
    for (std::size_t i = 0u; i != this->stacks.size (); ++i) {
        auto & stack = this->stacks [i];

        if (PtInRect (&stack.r, pt)) {
            NMMOUSE nm = {
                { hWnd, (UINT) GetDlgCtrlID (hWnd), 0 },
                i, (DWORD_PTR) stack.top, pt, HTCLIENT
            };
            switch (message) {
                case WM_RBUTTONUP: nm.hdr.code = NM_RCLICK; break;
                case WM_RBUTTONDOWN: nm.hdr.code = NM_RDOWN; break;
                case WM_LBUTTONDBLCLK: nm.hdr.code = NM_DBLCLK; break;
                case WM_RBUTTONDBLCLK: nm.hdr.code = NM_RDBLCLK; break;
            }

			bool command = false;
			if (this->hot.tag) {
				nm.dwHitInfo = HTMENU;

				if (message == WM_LBUTTONUP) {
					nm.hdr.code = NM_CLICK;
					nm.dwItemData = this->hot.tab;
					stack.top = this->hot.tab;
					command = true;
				}
			} else
            if (this->hot.close) {
                nm.dwHitInfo = HTCLOSE;

                if (message == WM_LBUTTONUP) {
                    nm.hdr.code = NM_CLICK;
                }
            } else {
                if (message == WM_LBUTTONDOWN) {
                    nm.hdr.code = NM_CLICK;
					command = true;
                }
            }

            if (nm.hdr.code) {
                MapWindowPoints (hWnd, NULL, &nm.pt, 1u);

				if (!SendMessage (GetParent (hWnd), WM_NOTIFY, nm.hdr.idFrom, (LPARAM) &nm)) {
					if (command && (this->current != i)) {

						auto hPrev = this->tabs [this->stacks [this->current].top].content;
						auto hNext = this->tabs [this->stacks [i].top].content;
						
						this->current = i;
						this->update ();

						if (hPrev) ShowWindow (hPrev, SW_HIDE);
						if (hNext) ShowWindow (hNext, SW_SHOW);

						SendMessage (GetParent (hWnd), WM_COMMAND, MAKEWPARAM (GetDlgCtrlID (hWnd), 0), (LPARAM) hWnd);
					}

				}
            }

            switch (message) {
                case WM_LBUTTONDOWN:
                    this->hot.down = true;
                    break;
                case WM_LBUTTONUP:
                    this->hot.down = false;
                    break;
            }
			if (nm.dwHitInfo != HTCLIENT) {
                this->update ();
                InvalidateRect (hWnd, NULL, FALSE);
            }
            break;
        }
    }
    return 0;
}
    
UINT TabControlState::mouse (POINT pt) {
	TabControlState::Hot new_hot;
	new_hot.down = this->hot.down;

    for (std::size_t i = 0u; i != this->stacks.size (); ++i) {
        const auto & stack = this->stacks [i];

        if (PtInRect (&stack.r, pt)) {
			new_hot.stack = i;
			for (std::size_t j = 0u; j != stack.tabs.size (); ++j) {
				if (PtInRect (&stack.tabs [j].tag, pt)) {
					new_hot.tag = true;
					new_hot.tab = stack.tabs [j].id;
					break;
				}
			}
            if (this->tabs [stack.top].close && PtInRect (&stack.rCloseButton, pt)) {
                new_hot.close = true;
            }
            break;
        }
    }

	if (this->hot != new_hot) {
        if (new_hot.stack != -1) {
            InvalidateRect (hWnd, &this->stacks [new_hot.stack].r, FALSE);
        }
        if (this->hot.stack != -1) {
            InvalidateRect (hWnd, &this->stacks [this->hot.stack].r, FALSE);
        }
        this->hot = new_hot;
    }

    if ((pt.x == -1) && (pt.y == -1)) {
        this->hot.down = false;
    } else {
        TRACKMOUSEEVENT tme = {
            sizeof (TRACKMOUSEEVENT),
            TME_LEAVE, hWnd, HOVER_DEFAULT
        };
        TrackMouseEvent (&tme);
    }
    return 0;
}
    /*
    LRESULT OnKeyDown (HWND hWnd, WPARAM wParam, LPARAM lParam) {
        NMKEY nmKey = {
            { hWnd, (UINT) GetDlgCtrlID (hWnd), (UINT) NM_KEYDOWN },
            (UINT) wParam, (UINT) lParam
        };
        if (!SendMessage (GetParent (hWnd), WM_NOTIFY, nmKey.hdr.idFrom, (LPARAM) &nmKey)) {
            switch (wParam) {
                case VK_LEFT:
                    Previous (hWnd);
                    break;
                case VK_RIGHT:
                    Next (hWnd);
                    break;
            }
        }
        return 0;
    }

    LRESULT OnHorizontalWheel (HWND hWnd, LONG distance, USHORT flags) {
        distance += (LONG) Get (hWnd, Index::WheelDelta);

        while (distance >= +WHEEL_DELTA) {
            distance -= WHEEL_DELTA;
            // Right (hWnd);
        }
        while (distance <= -WHEEL_DELTA) {
            distance += WHEEL_DELTA;
            // Left (hWnd);
        }

        Set (hWnd, Index::WheelDelta, distance);
        return 0;
    }*/

