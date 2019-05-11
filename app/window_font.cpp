#include "window.h"

Window::Fonts::Font::~Font () {
    DeleteObject (this->handle);
    this->handle = NULL;
}

bool Window::Fonts::Font::Update (HTHEME hTheme, UINT dpi, UINT dpiNULL, int id, const wchar_t * replace, int m, int d) {
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
            this->height = 72 * lf.lfHeight / 96;
        } else {
            this->height = -this->height;
        }

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
    }
    return false;
}
