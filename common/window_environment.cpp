#include "window_environment.h"
#include <VersionHelpers.h>
#include <shellapi.h>

extern "C" const IID IID_IImageList;

LRESULT WindowEnvironment::RefreshVisualMetrics (UINT dpiNULL) {
    if (ptrGetSystemMetricsForDpi) {
        for (auto i = 0; i != sizeof metrics / sizeof metrics [0]; ++i) {
            this->metrics [i] = ptrGetSystemMetricsForDpi (i, this->dpi);
        }
    } else {
        for (auto i = 0; i != sizeof metrics / sizeof metrics [0]; ++i) {
            this->metrics [i] = this->dpi * GetSystemMetrics (i) / dpiNULL;
        }
    }
    return 0;
}

SIZE WindowEnvironment::GetIconMetrics (IconSize size, UINT dpiNULL) {
    switch (size) {
        case IconSize::Small:
            return { metrics [SM_CXSMICON], metrics [SM_CYSMICON] };
        case IconSize::Start:
            return {
                (metrics [SM_CXICON] + metrics [SM_CXSMICON]) / 2,
                (metrics [SM_CYICON] + metrics [SM_CYSMICON]) / 2
            };
        case IconSize::Large:
        default:
            return { metrics [SM_CXICON], metrics [SM_CYICON] };

        case IconSize::Shell:
        case IconSize::Jumbo:
            if ((winver >= 6) || (size == IconSize::Shell)) { // XP doesn't have Jumbo
                if (HMODULE hShell32 = GetModuleHandle (L"SHELL32")) {
                    HRESULT (WINAPI * ptrSHGetImageList) (int, const GUID &, void **) = NULL;

                    if (winver >= 6) {
                        Symbol (hShell32, ptrSHGetImageList, "SHGetImageList");
                    } else {
                        Symbol (hShell32, ptrSHGetImageList, 727);
                    }
                    if (ptrSHGetImageList) {
                        HIMAGELIST list;
                        if (ptrSHGetImageList ((size == IconSize::Jumbo) ? SHIL_JUMBO : SHIL_EXTRALARGE,
                                               IID_IImageList, (void **) & list) == S_OK) {
                            int cx, cy;
                            if (ImageList_GetIconSize (list, &cx, &cy)) {
                                switch (size) {
                                    case IconSize::Shell: return { long (cx * dpi / dpiNULL), long (cy * dpi / dpiNULL) };
                                    case IconSize::Jumbo: return { long (cx * dpi / 96), long (cy * dpi / 96) };
                                }
                            }
                        }
                    }
                }
            }
            switch (size) {
                default:
                case IconSize::Shell: return { long (48 * dpi / dpiNULL), long (48 * dpi / dpiNULL) };
                case IconSize::Jumbo: return { long (256 * dpi / 96), long (256 * dpi / 96) };
            }
    }
}

WindowEnvironment::Font::~Font () {
    DeleteObject (this->handle);
    this->handle = NULL;
}

bool WindowEnvironment::Font::Update (const LOGFONT & lf) {
    if (auto hNewFont = CreateFontIndirect (&lf)) {
        if (this->handle != NULL) {
            DeleteObject (this->handle);
        }
        this->handle = hNewFont;
        return true;
    } else {
        if (this->handle == NULL) {
            this->handle = (HFONT) GetStockObject (DEFAULT_GUI_FONT);
        }
    }
    return false;
}

bool WindowEnvironment::Font::Update (HTHEME hTheme, UINT dpi, UINT dpiNULL, int id, const wchar_t * replace, int m, int d) {
    LOGFONT lf;
    if (GetThemeSysFont (hTheme, id, &lf) == S_OK) {
        if (replace) {
            std::wcscpy (lf.lfFaceName, replace);
        }
        lf.lfHeight = MulDiv (lf.lfHeight, dpi, dpiNULL);
        if (d != 0) {
            lf.lfHeight = MulDiv (lf.lfHeight, m, d);
        }
        if (lf.lfHeight > 0) {
            this->height = lf.lfHeight;
        } else {
            this->height = 96 * -lf.lfHeight / 72;
        }

        if (this->width != FW_DONTCARE) {
            lf.lfWeight = this->width;
        }
        if (this->make_italic) {
            lf.lfItalic = TRUE;
        }
        if (this->make_underlined) {
            lf.lfUnderline = TRUE;
        }
        return this->Update (lf);
    } else
        return false;
}

