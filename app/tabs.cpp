#include "tabs.h"
#include "appapi.h"
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
			std::intptr_t   id;
			RECT		    tag = { 0,0,0,0 };
		};

        std::vector <TabRef> tabs;
        std::intptr_t   top; // ID of top tab
        std::uint8_t    progress;
        std::uint8_t    left : 1;
        std::uint8_t    right : 1;
        std::uint8_t    locked : 1;
        long            text_width = 0;
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
        std::size_t first_overflow_stack = -1;

		struct Hot {
			std::size_t   stack = -1; // index
			std::intptr_t tab = 0; // tab ID

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
        void stack (std::intptr_t which, std::intptr_t into, bool after) override;
        bool request (std::intptr_t tab) override;
        bool request_stack (std::size_t index) override;
        
        void repaint (HDC, RECT rc);
        UINT hittest (POINT);
        UINT mouse (POINT);
        UINT click (UINT, POINT);
        UINT key (WPARAM, LPARAM);
        UINT wheel (SHORT distance, USHORT flags, POINT);
        bool next (USHORT flags);
        bool prev (USHORT flags);

    private:
        void update_stacks_state ();
        void update_visual_representation ();
        StackState * get_tab_stack (std::intptr_t tab, std::vector <StackState::TabRef> ::iterator * = nullptr);

        bool switch_to_stack_tab (std::size_t stack);
        bool switch_to_stack_tab (std::size_t stack, std::intptr_t tab);
        bool switch_to_stack_tab_unchecked (std::size_t stack, std::intptr_t tab);
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
                case WM_MOUSEMOVE:
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                case WM_RBUTTONUP:
                case WM_RBUTTONDOWN:
                case WM_MBUTTONUP:
                case WM_MBUTTONDOWN:
                    if (auto ptr = state (hWnd)) {
                        if (auto tt = ptr->hToolTipControl) {
                            MSG m = {
                                hWnd, message, wParam, lParam,
                                (DWORD) GetMessageTime (), (LONG) GetMessagePos ()
                            };
                            SendMessage (tt, TTM_RELAYEVENT,
                                         (WPARAM) ((message == WM_MOUSEMOVE) ? GetMessageExtraInfo () : 0),
                                         (LPARAM) &m);
                        }
                    }
            }

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
                    
                case WM_KEYDOWN:
                    return state (hWnd)->key (wParam, lParam);
                case WM_MOUSEHWHEEL:
                    return state (hWnd)->wheel (HIWORD (wParam), LOWORD (wParam), { (short) LOWORD (lParam), (short) HIWORD (lParam) });
                    
                case WM_CHAR: {
                    NMCHAR nm = {
                        { hWnd, (UINT) GetDlgCtrlID (hWnd), (UINT) NM_CHAR },
                        (UINT) wParam, 0u, 0u
                    };
                    if (auto tab = SendMessage (GetParent (hWnd), WM_NOTIFY, nm.hdr.idFrom, (LPARAM) &nm)) {
                        state (hWnd)->request (tab);
                    } else {
                        // ??? typing tab text selects tab ???
                    }
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

                case WM_NOTIFY:
                    if (auto nm = reinterpret_cast <NMHDR *> (lParam)) {
                        auto self = state (hWnd);

                        if (nm->hwndFrom == self->hToolTipControl) {
                            if (nm->code == TTN_GETDISPINFO) {
                                auto nmTT = reinterpret_cast <NMTTDISPINFO  *> (nm);
                                auto & tab = self->tabs [nmTT->lParam];

                                if (tab.tip.empty ()) {
                                    if (tab.ellipsis) {
                                        nmTT->lpszText = const_cast <LPWSTR> (tab.text.c_str ());
                                    }
                                } else {
                                    nmTT->lpszText = const_cast <LPWSTR> (tab.tip.c_str ());
                                }
                            }
                        }
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

StackState * TabControlState::get_tab_stack (std::intptr_t tab, std::vector <StackState::TabRef> ::iterator * ii) {
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

void TabControlState::stack (std::intptr_t which, std::intptr_t with, bool after) {
    this->update_stacks_state ();

    std::vector <StackState::TabRef> ::iterator from_ii;
    std::vector <StackState::TabRef> ::iterator into_ii;
    auto from = this->get_tab_stack (which, &from_ii);
    auto into = this->get_tab_stack (with, &into_ii);

    if (from && into) {
        if (into->tabs.size () == 1) {
            into->tabs.reserve (8);
        }
        /*if (after) {
            into->tabs.insert (into_ii, *from_ii);
        } else {*/
            into->tabs.insert (into->tabs.end (), *from_ii);
        //}
        from->tabs.erase (from_ii);
    }
}

bool TabControlState::request (std::intptr_t tab) {
    if (auto stack = this->get_tab_stack (tab)) {
        return this->switch_to_stack_tab (stack - &this->stacks [0], tab);
    } else
        return false;
}

bool TabControlState::request_stack (std::size_t index) {
    return this->switch_to_stack_tab (index);
}

bool TabControlState::switch_to_stack_tab (std::size_t i) {
    if (i < this->stacks.size ()) {
        return this->switch_to_stack_tab_unchecked (i, this->stacks [i].top);
    } else
        return false;
}

bool TabControlState::switch_to_stack_tab (std::size_t i, std::intptr_t tab) {
    if (i < this->stacks.size ()) {

        auto nn = this->stacks [i].tabs.size ();
        auto ii = 0u;

        for (; ii != nn; ++ii) {
            if (this->stacks [i].tabs [ii].id == tab) {
                return this->switch_to_stack_tab_unchecked (i, tab);
            }
        }
    }
    return false;
}

bool TabControlState::switch_to_stack_tab_unchecked (std::size_t i, std::intptr_t tab) {
    if ((this->current != i) || (this->stacks [i].top != tab)) {

        auto hPrev = this->tabs [this->stacks [this->current].top].content;
        auto hNext = this->tabs [tab].content;

        this->stacks [i].top = tab;
        this->current = i;
        this->update ();

        if (hPrev) ShowWindow (hPrev, SW_HIDE);
        if (hNext) ShowWindow (hNext, SW_SHOW);

        SendMessage (GetParent (hWnd), WM_COMMAND, MAKEWPARAM (GetDlgCtrlID (hWnd), 0), (LPARAM) hWnd);
        return true;
    } else
        return false;
}

void TabControlState::update_stacks_state () {
    bool changed = false;

    // create stacks for new tabs
    for (const auto & tab : this->tabs) {
        const auto id = tab.first;
        if (this->get_tab_stack (id) == nullptr) {
            this->stacks.push_back (StackState ());
			this->stacks.back ().tabs.push_back ({ id });
            this->stacks.back ().top = id;

            if (this->hToolTipControl) {
                TOOLINFO tt = {
                    sizeof (TOOLINFO), 0, this->hWnd,
                    this->stacks.size () - 1, {0,0,0,0},
                    NULL, LPSTR_TEXTCALLBACK, (LPARAM) id, NULL
                };
                SendMessage (this->hToolTipControl, TTM_ADDTOOL, 0, (LPARAM) &tt);
            }
            changed = true;
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

                if (top && (i->top == this->current)) {
                    if (auto h = this->tabs [ti->id].content) {
                        ShowWindow (h, SW_HIDE);
                        SendMessage (GetParent (hWnd), WM_COMMAND, MAKEWPARAM (GetDlgCtrlID (hWnd), 0), (LPARAM) hWnd); // ??
                    }
                }

                ti = i->tabs.erase (ti);
                te = i->tabs.end ();

                if (top) {
                    if (ti == te) {
                        if (!i->tabs.empty ()) {
                            i->top = i->tabs.back ().id;
                            changed = true;
                        }
                    } else {
                        i->top = ti->id;
                        changed = true;
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
                        SendMessage (GetParent (hWnd), WM_COMMAND, MAKEWPARAM (GetDlgCtrlID (hWnd), 0), (LPARAM) hWnd);
					}
				}
			} else
			if (index < this->current) {
				--this->current;
			}

            if (this->hToolTipControl) {
                TOOLINFO tt = {
                    sizeof (TOOLINFO), 0, this->hWnd,
                    this->stacks.size (), {0,0,0,0},
                    NULL, LPSTR_TEXTCALLBACK, 0, NULL
                };
                SendMessage (this->hToolTipControl, TTM_DELTOOL, 0, (LPARAM) &tt);
            }
            changed = true;
        } else {
			++i;
        }
    }

    // combine progress info for stacks
    for (auto & stack : this->stacks) {
        stack.progress = 0;

        for (auto tabref : stack.tabs) {
            const auto & tab = this->tabs [tabref.id];

            if (tab.progress != 0) {
                if (stack.progress) {
                    stack.progress = std::min (stack.progress, tab.progress);
                } else {
                    stack.progress = tab.progress;
                }
            }
        }
    }

    // give app feedback on current stacking
    for (std::size_t i = 0u; i != this->stacks.size (); ++i) {
        for (auto tabref : this->stacks [i].tabs) {
            this->tabs [tabref.id].stack_index = i;
        }
    }

    if (changed) {
        NMHDR nm = { this->hWnd, (UINT) GetDlgCtrlID (this->hWnd), (UINT) RBN_LAYOUTCHANGED };
        SendMessage (GetParent (this->hWnd), WM_NOTIFY, nm.idFrom, (LPARAM) &nm);
    }
}

void TabControlState::update () {
    this->update_stacks_state ();
    this->update_visual_representation ();
    InvalidateRect (hWnd, NULL, FALSE);
}

void TabControlState::update_visual_representation () {
    this->overflow.clear ();
    this->first_overflow_stack = -1;

    const auto size = GetClientSize (hWnd);
    for (auto & stack : this->stacks) {
        stack.left = true;
        stack.right = true;
        stack.locked = false;
        stack.r.top = 2 * dpi / 96;
        stack.r.left = 0;
        stack.text_width = 0;
    }
    if (!this->stacks.empty ()) {
        this->stacks.front ().r.left = 2 * dpi / 96;
    }

    if (auto hDC = GetDC (hWnd)) {
        auto hPreviousFont = SelectObject (hDC, this->font);
        
		auto fullwidth = 6 * dpi / 96;
		auto locked = 0u;
        auto width = 2 * dpi / 96;

        auto xextent = 12 * dpi / 96;

        this->minimum.cy = 0;
        this->minimum.cx = 2 * width + this->min_tab_width + 2 * xextent;

        for (auto & stack : this->stacks) {
            stack.r.bottom = DrawTextEx (hDC, const_cast <LPTSTR> (this->tabs [stack.top].text.c_str ()),
                                         -1, &stack.r, DT_CALCRECT | DT_SINGLELINE, NULL);
            stack.text_width = stack.r.right;
            this->minimum.cy = std::max (this->minimum.cy, stack.r.bottom);

            if (this->tabs [stack.top].fit) {
                if (this->badges) {
                    stack.r.right += (std::uint16_t) (10 * dpi / 96);
                }
                this->minimum.cx += stack.r.right - stack.r.left + xextent;
                stack.locked = true;
                ++locked;
            } else {
                if (this->max_tab_width != 0) {
                    stack.r.right = this->max_tab_width;
                }
                if (stack.r.right < this->min_tab_width) {
                    stack.r.right = this->min_tab_width;
                }
            }

            stack.r.right += xextent;
			fullwidth += stack.r.right;
		}
        if (this->minimum.cy == 0) {
            RECT r = { 0,0,0,0 };
            this->minimum.cy = DrawTextEx (hDC, const_cast <LPTSTR> (L"y"), 1, &r, DT_CALCRECT | DT_SINGLELINE, NULL);
        }

        for (auto & stack : this->stacks) {
            stack.r.bottom = size.cy - 1 * dpi / 96;
        }

		bool tight = (fullwidth > size.cx);
        long reduce = 0;

        if (tight) {
            while (auto flexibility = this->stacks.size () - locked) {
                reduce = long ((fullwidth - size.cx) / flexibility);

                bool any = false;
                for (auto & stack : this->stacks) {
                    if (!stack.locked) {
                        if (stack.r.right - reduce < this->min_tab_width + xextent) {
                            
                            fullwidth -= stack.r.right;
                            fullwidth += this->min_tab_width + xextent;
                            stack.r.right = this->min_tab_width + xextent;
                            stack.locked = true;

                            ++locked;
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
			if (!stack.locked) {
				if (stack.r.right >= reduce) {
                    stack.r.right -= reduce;
				}
			}

            stack.r.left = width;
            stack.r.right += stack.r.left;

            stack.rContent = stack.r;
            stack.rContent.top += 4 * dpi / 96;
            stack.rContent.left += 5 * dpi/ 96;
            stack.rContent.right -= 4 * dpi / 96;

            if (this->tabs [stack.top].close) {
                stack.rContent.right = stack.rCloseButton.left;
                stack.rCloseButton.top = stack.r.top + dpi * 4 / 96;
                stack.rCloseButton.left = stack.r.right - stack.r.bottom + dpi * 6 / 96;
                stack.rCloseButton.right = stack.r.right - dpi * 4 / 96;
                stack.rCloseButton.bottom = stack.r.bottom - dpi * 4 / 96;
            }
            this->tabs [stack.top].ellipsis = (stack.text_width > (stack.rContent.right - stack.rContent.left));

            width = stack.r.right;

			for (std::size_t i = 0, n = stack.tabs.size (); i != n; ++i) {
				stack.tabs [i].tag.top = stack.r.top;
				stack.tabs [i].tag.bottom = stack.r.top + 5 * dpi / 96;
				stack.tabs [i].tag.left = stack.r.left + (1 * dpi / 96) + (LONG (i) + 0) * (stack.r.right - stack.r.left - (2 * dpi / 96)) / long (n);
				stack.tabs [i].tag.right = stack.r.left + (1 * dpi / 96) + (LONG (i) + 1) * (stack.r.right - stack.r.left - (2 * dpi / 96)) / long (n);

				if (stack.top == stack.tabs [i].id) { // current tab in stack
					stack.tabs [i].tag.bottom = stack.tabs [i].tag.top;
				}
                if (width > size.cx) {
                    if (this->overflow.empty ()) {
                        this->first_overflow_stack = &stack - &this->stacks [0]; // index
                    }
                    this->overflow.push_back (stack.tabs [i].id);
                }
			}
        }

        if (this->hToolTipControl) {
            TOOLINFO tt = {
                sizeof (TOOLINFO), 0, this->hWnd,
                0, {0,0,0,0}, NULL, NULL, 0, NULL
            };

            auto n = this->stacks.size ();
            for (tt.uId = 0u; tt.uId != n; ++tt.uId) {
                if (SendMessage (this->hToolTipControl, TTM_GETTOOLINFO, 0, (LPARAM) &tt)) {
                    if (!EqualRect (&tt.rect, &this->stacks [tt.uId].r) || (tt.lParam != this->stacks [tt.uId].top)) {
                        tt.rect = this->stacks [tt.uId].r;
                        tt.lParam = this->stacks [tt.uId].top;
                        SendMessage (this->hToolTipControl, TTM_SETTOOLINFO, 0, (LPARAM) &tt);
                    }
                }
            }
        }

        if (this->current < this->stacks.size ()) {
            auto stack = &this->stacks [this->current];
            stack->r.top = 0;
            stack->r.left -= 2 * dpi / 96;
            stack->r.right += 2 * dpi / 96;
            stack->r.bottom = size.cy;
            stack->rContent.top -= 2 * dpi / 96;
            stack->rContent.bottom -= 1 * dpi / 96;
            stack->rCloseButton.top -= 2;
            stack->rCloseButton.bottom -= 2;

            if (this->current > 0) {
                this->stacks [this->current - 1].right = false;
                this->stacks [this->current - 1].r.right -= 2 * dpi / 96;
            }
            if (this->current < this->stacks.size () - 1) {
                this->stacks [this->current + 1].left = false;
                this->stacks [this->current + 1].r.left += 2 * dpi / 96;
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

    this->minimum.cy += 11 * dpi / 96;
}

UINT TabControlState::hittest (POINT pt) {
    if (ScreenToClient (hWnd, &pt)) {
        for (const auto & stack : this->stacks)
            if (PtInRect (&stack.r, pt))
                return HTCLIENT;
    }
    return HTTRANSPARENT;
};

struct TabControlVisualStyle {

};
TabControlVisualStyle DarkTabControlVisualStyle = {
};
TabControlVisualStyle LightTabControlVisualStyle = {
};

void TabControlState::repaint (HDC _hDC, RECT rcInvalidated) {
    this->update_stacks_state ();
    this->update_visual_representation ();

    RECT rc = GetClientRect (hWnd);
    HDC hDC = NULL;
    HANDLE hBuffered = NULL;
    HBITMAP hOffBitmap = NULL;
    HGDIOBJ hOffOld = NULL;

    if (ptrBeginBufferedPaint) {
        hBuffered = ptrBeginBufferedPaint (_hDC, &rcInvalidated, BPBF_TOPDOWNDIB, NULL, &hDC);
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

        RECT rStackVisible;
        if (IntersectRect (&rStackVisible, &stack.r, &rcInvalidated)) {

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
            }

            if (stack.progress) {
                RECT rProgress = stack.r;
                InflateRect (&rProgress, -1 * dpi / 96, -1 * dpi / 96);
                rProgress.left += stack.progress * (rProgress.right - rProgress.left) / 255;

                SetDCBrushColor (hDC, 0xFFDDCC);
                FillRect (hDC, &rProgress, (HBRUSH) GetStockObject (DC_BRUSH));
            }

            if (this->tabs [stack.top].close) {
                if (this->theme) {
                    RECT r = stack.rCloseButton;
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
                } else {
                    RECT r = stack.rCloseButton;
                    auto style = DFCS_CAPTIONCLOSE;
                    r.top += 1;
                    r.bottom += 1;

                    if (i != this->current) {
                        InflateRect (&r, 1, 1);
                        DrawEdge (hDC, &r, BDR_SUNKENOUTER, BF_RECT);
                        InflateRect (&r, -1, -1);
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
                        style |= DFCS_FLAT;
                        r.left -= 1;
                        r.bottom += 1;
                    }

                    DrawFrameControl (hDC, &r, DFC_CAPTION, style);
                }
            }

            // stack

            for (const auto & tabref : stack.tabs) {
				auto r = tabref.tag;
                if (r.bottom != r.top) {
                    r.left += 1 * dpi / 96;
                    r.right -= 1 * dpi / 96;
                    r.bottom -= 1 * dpi / 96;
                    
                    if (!this->theme) {
                        r.top += 1;
                        if (i != this->current) {
                            r.top += 1;
                        }
                    }

					if (this->hot.tag && (tabref.id == this->hot.tab)) {
						if (this->hot.down) {
							FillRect (hDC, &r, (HBRUSH) GetStockObject (BLACK_BRUSH));
						} else {
							FillRect (hDC, &r, GetSysColorBrush (COLOR_HOTLIGHT));
						}
					} else {
                        if (auto badge = this->tabs [tabref.id].badge) {
                            SetDCBrushColor (hDC, 0x6060C0);
                            FillRect (hDC, &r, (HBRUSH) GetStockObject (DC_BRUSH));
                        } else {
                            FillRect (hDC, &r, GetSysColorBrush (COLOR_3DSHADOW));
                        }
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
                        rText.left += bmi.bmWidth + 2 * dpi / 96;
                    }

                    DeleteObject (ii.hbmColor);
                    DeleteObject (ii.hbmMask);
                }
            }

            DrawTextEx (hDC, const_cast <LPTSTR> (this->tabs [stack.top].text.c_str ()),
                        -1, &rText, DT_TOP | DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS, NULL);

            // badge

            if (auto badge = this->tabs [stack.top].badge) {
                wchar_t text [3];
                if (badge < 100) {
                    std::swprintf (text, 3, L"%u", badge);
                } else {
                    std::swprintf (text, 3, L"##");
                }

                RECT r = stack.rContent;
                SetTextColor (hDC, 0x0000FF);
                SelectObject (hDC, this->font);
                DrawTextEx (hDC, text, -1, &r, DT_BOTTOM | DT_RIGHT | DT_SINGLELINE, NULL);
            }

            if (ptrBufferedPaintSetAlpha && hBuffered) {
                if (i < this->first_overflow_stack) {
                    ptrBufferedPaintSetAlpha (hBuffered, &rStackVisible, 255);
                } else {
                    BufferedPaintPremultiply (hBuffered, rStackVisible, 128, 128);
                }
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
                (DWORD_PTR) stack.top,
                (DWORD_PTR) this->tabs [stack.top].content,
                pt, HTCLIENT
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
					nm.dwItemSpec = this->hot.tab;
                    nm.dwItemData = (DWORD_PTR) this->tabs [this->hot.tab].content;
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
					if (command) {
                        if (this->hot.tag) {
                            this->switch_to_stack_tab_unchecked (i, this->hot.tab);
                        } else {
                            this->switch_to_stack_tab (i);
                        }
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

static USHORT MakeKbFlags () {
    USHORT flags = 0;
    flags |= (GetKeyState (VK_SHIFT) & 0x8000 ? MK_SHIFT : 0);
    flags |= (GetKeyState (VK_CONTROL) & 0x8000 ? MK_CONTROL : 0);
    return flags;
}

bool TabControlState::next (USHORT flags = MakeKbFlags ()) {
    if (flags & MK_SHIFT) {
        if (this->current < this->stacks.size ()) {
            auto & stack = this->stacks [this->current];
            auto n = stack.tabs.size () - 1;

            for (auto i = 0u; i != n; ++i) {
                if (stack.top == stack.tabs [i].id) {
                    return this->switch_to_stack_tab_unchecked (this->current, stack.tabs [i + 1].id);
                }
            }
        }
        return false;
    } else
        return this->switch_to_stack_tab (this->current + 1);
}
bool TabControlState::prev (USHORT flags = MakeKbFlags ()) {
    if (flags & MK_SHIFT) {
        if (this->current < this->stacks.size ()) {
            auto & stack = this->stacks [this->current];
            
            for (auto i = 1u; i != stack.tabs.size (); ++i) {
                if (stack.top == stack.tabs [i].id) {
                    return this->switch_to_stack_tab_unchecked (this->current, stack.tabs [i - 1].id);
                }
            }
        }
        return false;
    } else
        return this->switch_to_stack_tab (this->current - 1);
}

UINT TabControlState::key (WPARAM vk, LPARAM flags) {
    NMKEY nmKey = {
        { hWnd, (UINT) GetDlgCtrlID (hWnd), (UINT) NM_KEYDOWN },
        (UINT) vk, (UINT) flags
    };
    if (!SendMessage (GetParent (hWnd), WM_NOTIFY, nmKey.hdr.idFrom, (LPARAM) &nmKey)) {
        switch (vk) {
            case VK_LEFT:
                this->prev ();
                break;
            case VK_RIGHT:
                this->next ();
                break;
        }
    }
    return 0;
}
UINT TabControlState::wheel (SHORT distance, USHORT flags, POINT mouse) {
    distance += this->wheel_delta;

    while (distance >= +WHEEL_DELTA) {
        distance -= WHEEL_DELTA;
        this->next (flags);
    }
    while (distance <= -WHEEL_DELTA) {
        distance += WHEEL_DELTA;
        this->prev (flags);
    }

    this->wheel_delta = distance;
    return 0;
}
