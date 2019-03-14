#include "appapi.h"
#include <VersionHelpers.h>

HRESULT (WINAPI * ptrBufferedPaintInit) () = NULL;
HRESULT (WINAPI * ptrBufferedPaintSetAlpha) (HPAINTBUFFER, const RECT *, BYTE) = NULL;
HPAINTBUFFER (WINAPI * ptrBeginBufferedPaint) (HDC, const RECT *, BP_BUFFERFORMAT, const BP_PAINTPARAMS *, HDC *) = NULL;
HRESULT (WINAPI * ptrEndBufferedPaint) (HPAINTBUFFER, BOOL) = NULL;
HDC (WINAPI * ptrGetBufferedPaintDC) (HPAINTBUFFER) = NULL;
HRESULT (WINAPI * ptrGetBufferedPaintBits) (HPAINTBUFFER, RGBQUAD **, int *) = NULL;
HRESULT (WINAPI * ptrDrawThemeTextEx) (HTHEME, HDC, int, int, LPCWSTR, int, DWORD, LPRECT, const DTTOPTS *) = NULL;

UINT (WINAPI * ptrGetImmersiveColorTypeFromName) (LPCWSTR name) = NULL; // 96
LPCWSTR* (WINAPI * ptrGetImmersiveColorNamedTypeByIndex) (UINT index) = NULL; // 100
UINT (WINAPI * ptrGetImmersiveUserColorSetPreference) (BOOL reload, BOOL skip) = NULL; // 98
UINT (WINAPI * ptrGetImmersiveColorFromColorSetEx) (UINT set, UINT type, BOOL ignoreHighContrast, UINT cachemode) = NULL; // 95

BOOL (WINAPI * ptrAllowDarkModeForApp) (BOOL) = NULL;
BOOL (WINAPI * ptrAllowDarkModeForWindow) (HWND, BOOL) = NULL;
void (WINAPI * ptrFlushMenuThemes) () = NULL;
void (WINAPI * ptrRefreshImmersiveColorPolicyState) () = NULL;

BOOL (WINAPI * ptrDwmDefWindowProc) (HWND, UINT, WPARAM, LPARAM, LRESULT *) = NULL;
HRESULT (WINAPI * ptrDwmGetWindowAttribute) (HWND, DWORD, PVOID, DWORD) = NULL;
HRESULT (WINAPI * ptrDwmSetWindowAttribute) (HWND, DWORD, LPCVOID, DWORD) = NULL;
HRESULT (WINAPI * ptrDwmExtendFrameIntoClientArea) (HWND, const MARGINS *) = NULL;
HRESULT (WINAPI * ptrDwmGetColorizationColor) (DWORD *, BOOL *) = NULL;
HRESULT (WINAPI * ptrDwmIsCompositionEnabled) (BOOL *) = NULL;

BOOL (WINAPI * ptrEnableNonClientDpiScaling) (HWND) = NULL;
BOOL (WINAPI * ptrChangeWindowMessageFilter) (UINT, DWORD) = NULL;
UINT (WINAPI * pfnGetDpiForSystem) () = NULL;
UINT (WINAPI * pfnGetDpiForWindow) (HWND) = NULL;
int (WINAPI * ptrGetSystemMetricsForDpi) (int, UINT) = NULL;
DPI_AWARENESS_CONTEXT (WINAPI * ptrGetWindowDpiAwarenessContext) (HWND) = NULL;
BOOL (WINAPI * ptrAreDpiAwarenessContextsEqual) (DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT) = NULL;

HRESULT (WINAPI * ptrLoadIconWithScaleDown) (HINSTANCE, PCWSTR, int, int, HICON *) = NULL;
HRESULT (WINAPI * ptrTaskDialogIndirect) (const TASKDIALOGCONFIG *, int * button, int * radio, BOOL * check) = NULL;

void AppApiInitialize () {
    if (HMODULE hUxTheme = GetModuleHandle (L"UXTHEME")) {
        Symbol (hUxTheme, ptrBufferedPaintInit, "BufferedPaintInit");
        Symbol (hUxTheme, ptrBeginBufferedPaint, "BeginBufferedPaint");
        Symbol (hUxTheme, ptrBufferedPaintSetAlpha, "BufferedPaintSetAlpha");
        Symbol (hUxTheme, ptrGetBufferedPaintBits, "GetBufferedPaintBits");
        Symbol (hUxTheme, ptrGetBufferedPaintDC, "GetBufferedPaintDC");
        Symbol (hUxTheme, ptrEndBufferedPaint, "EndBufferedPaint");
        Symbol (hUxTheme, ptrDrawThemeTextEx, "DrawThemeTextEx");

        if (IsWindows8OrGreater ()) {
            Symbol (hUxTheme, ptrGetImmersiveColorTypeFromName, 96);
            Symbol (hUxTheme, ptrGetImmersiveColorFromColorSetEx, 95);
            Symbol (hUxTheme, ptrGetImmersiveColorNamedTypeByIndex, 100);
            Symbol (hUxTheme, ptrGetImmersiveUserColorSetPreference, 98);

        }
        if (IsWindowsBuildOrGreater (10, 0, 17763)) {
            Symbol (hUxTheme, ptrFlushMenuThemes, 136);
            Symbol (hUxTheme, ptrAllowDarkModeForApp, 135);
            Symbol (hUxTheme, ptrAllowDarkModeForWindow, 133);
            Symbol (hUxTheme, ptrRefreshImmersiveColorPolicyState, 104);
        }
    }
    if (HMODULE hUser32 = GetModuleHandle (L"USER32")) {
        Symbol (hUser32, ptrEnableNonClientDpiScaling, "EnableNonClientDpiScaling");
        Symbol (hUser32, ptrChangeWindowMessageFilter, "ChangeWindowMessageFilter");

        Symbol (hUser32, pfnGetDpiForSystem, "GetDpiForSystem");
        Symbol (hUser32, pfnGetDpiForWindow, "GetDpiForWindow");
        Symbol (hUser32, ptrGetSystemMetricsForDpi, "GetSystemMetricsForDpi");
        Symbol (hUser32, ptrGetWindowDpiAwarenessContext, "GetWindowDpiAwarenessContext");
        Symbol (hUser32, ptrAreDpiAwarenessContextsEqual, "AreDpiAwarenessContextsEqual");
    }
    if (HMODULE hComCtl32 = GetModuleHandle (L"COMCTL32")) {
        Symbol (hComCtl32, ptrLoadIconWithScaleDown, "LoadIconWithScaleDown");
        Symbol (hComCtl32, ptrTaskDialogIndirect, "TaskDialogIndirect");
    }
    if (HMODULE hDwmApi = LoadLibrary (L"DWMAPI")) {
        Symbol (hDwmApi, ptrDwmDefWindowProc, "DwmDefWindowProc");
        Symbol (hDwmApi, ptrDwmGetWindowAttribute, "DwmGetWindowAttribute");
        Symbol (hDwmApi, ptrDwmSetWindowAttribute, "DwmSetWindowAttribute");
        Symbol (hDwmApi, ptrDwmIsCompositionEnabled, "DwmIsCompositionEnabled");
        Symbol (hDwmApi, ptrDwmGetColorizationColor, "DwmGetColorizationColor");
        Symbol (hDwmApi, ptrDwmExtendFrameIntoClientArea, "DwmExtendFrameIntoClientArea");
    }
}

UINT GetDPI (HWND hWnd) {
    if (hWnd != NULL) {
        if (pfnGetDpiForWindow)
            return pfnGetDpiForWindow (hWnd);
    } else {
        if (pfnGetDpiForSystem)
            return pfnGetDpiForSystem ();
    }
    if (HDC hDC = GetDC (hWnd)) {
        auto dpi = GetDeviceCaps (hDC, LOGPIXELSX);
        ReleaseDC (hWnd, hDC);
        return dpi;
    } else
        return USER_DEFAULT_SCREEN_DPI;
}

bool AreDpiApisScaled (HWND hWnd) {
    if (ptrGetWindowDpiAwarenessContext && ptrAreDpiAwarenessContextsEqual) {
        return ptrAreDpiAwarenessContextsEqual (ptrGetWindowDpiAwarenessContext (hWnd), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else
        return false;
}

RECT FixWindowCoordinates (int x, int y, int w, int h) {
    if ((w > 0) && (h > 0)) {
        RECT r = { x, y, x + w, y + h };

        auto monitor = MonitorFromRect (&r, MONITOR_DEFAULTTONULL);
        if (monitor != NULL) {

            MONITORINFO info;
            info.cbSize = sizeof info;

            if (GetMonitorInfo (monitor, &info)) {
                RECT rX;
                if (IntersectRect (&rX, &r, &info.rcWork)) {

                    // use the provided coordinates if at least 1/16 of window is visible

                    if (((rX.right - rX.left) * (rX.bottom - rX.top)) >= (w * h) / 16) {
                        return { x, y, w, h };
                    }
                }
            }
        }
    }
    return { CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT };
}

bool IsLastWindow (HWND hWnd) {
    auto atom = GetClassLongPtr (hWnd, GCW_ATOM);
    return FindWindowEx (NULL, hWnd, (LPCTSTR) atom, NULL) == NULL
        && FindWindowEx (NULL, NULL, (LPCTSTR) atom, NULL) == hWnd;
}

HICON LoadBestIcon (HMODULE hModule, LPCWSTR resource, SIZE size) {
    HICON hNewIcon = NULL;
    if (size.cx > 256) size.cx = 256;
    if (size.cy > 256) size.cy = 256;

    if (ptrLoadIconWithScaleDown) {
        if (ptrLoadIconWithScaleDown (hModule, resource, size.cx, size.cy, &hNewIcon) != S_OK) {
            hNewIcon = NULL;
        }
    }
    if (hNewIcon)
        return hNewIcon;
    else
        return (HICON) LoadImage (hModule, resource, IMAGE_ICON, size.cx, size.cy, LR_DEFAULTCOLOR);
}

HBRUSH CreateSolidBrushEx (COLORREF color, unsigned char alpha) {
    auto b = alpha * ((color & 0x00FF0000) >> 16);
    auto g = alpha * ((color & 0x0000FF00) >> 8);
    auto r = alpha * ((color & 0x000000FF) >> 0);

    BITMAPINFO bi = {
        {   sizeof (BITMAPINFO),
            1,1,1, 32,BI_RGB, 0,0,0,0,0
        }, {
            (BYTE) ((b * 0x8081) >> 0x17), // divide by 255
            (BYTE) ((g * 0x8081) >> 0x17),
            (BYTE) ((r * 0x8081) >> 0x17),
            alpha
        }
    };
    return CreateDIBPatternBrushPt (&bi, DIB_RGB_COLORS);
}
HBRUSH CreateSolidBrushEx (COLORREF color) {
    return CreateSolidBrushEx (color, unsigned (color) >> 24u);
}
bool IsColorDark (COLORREF color) {
    return 2 * GetRValue (color) + 5 * GetGValue (color) + GetBValue (color) <= 1024; // MS default
    // return 299 * GetRValue (color) + 587 * GetGValue (color) + 114 * GetBValue (color) <= 128000; // YIQ?
}

HRESULT DrawCompositedText (HDC hDC, HTHEME hTheme, HFONT hFont,
                            LPCWSTR string, int length, DWORD format, RECT r,
                            COLORREF color, UINT glow) {
    SelectObject (hDC, hFont);

    DTTOPTS options;
    options.dwSize = sizeof options;
    options.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
    options.crText = color;

    if (glow) {
        options.dwFlags |= DTT_GLOWSIZE;
        options.iGlowSize = glow;
    }
    return ptrDrawThemeTextEx (hTheme, hDC, 0, 0,
                               string, length, format & ~(DT_MODIFYSTRING | DT_CALCRECT),
                               &r, &options);
}

void Cursors::update () {
    this->wait = LoadCursor (NULL, IDC_WAIT);
    this->arrow = LoadCursor (NULL, IDC_ARROW);
    this->working = LoadCursor (NULL, IDC_APPSTARTING);
    this->vertical = LoadCursor (NULL, IDC_SIZENS);
    this->horizontal = LoadCursor (NULL, IDC_SIZEWE);
}
void Design::update () {
    if (ptrRefreshImmersiveColorPolicyState) {
        ptrRefreshImmersiveColorPolicyState ();
    }
    if (ptrFlushMenuThemes) {
        ptrFlushMenuThemes ();
    }

    BOOL compositionEnabled = FALSE;
    if (ptrDwmIsCompositionEnabled) {
        ptrDwmIsCompositionEnabled (&compositionEnabled);
    }

    this->composited = compositionEnabled;
    this->nice = (this->composited || (IsAppThemed () && IsWindowsVistaOrGreater ()));
    this->alpha = this->composited && !IsWindows10OrGreater ();

    if (ptrDwmGetColorizationColor) {
        BOOL opaque = TRUE;
        DWORD color;
        if (ptrDwmGetColorizationColor (&color, &opaque) == S_OK) {
            this->colorization.active = RGB (GetBValue (color), GetGValue (color), GetRValue (color));
        } else {
            this->colorization.active = GetSysColor (COLOR_WINDOW);
        }
        this->colorization.inactive = this->colorization.active;
    }

    if (ptrGetImmersiveUserColorSetPreference) {
        this->colorset = ptrGetImmersiveUserColorSetPreference (FALSE, FALSE);
        /*
        for (auto i = 0; auto name = ptrGetImmersiveColorNamedTypeByIndex (i); ++i) {

            auto type = ptrGetImmersiveColorTypeFromName ((L"Immersive" + std::wstring (*name)).c_str ());
            auto color = ptrGetImmersiveColorFromColorSetEx (this->colorset, type, false, 0);

            wchar_t szcolor [9];
            _snwprintf (szcolor, 9, L"%08X", color);

            raddi::log::event (0xA1F0, i, *name, type, szcolor);
        }*/
    }

    HIGHCONTRAST hc;
    hc.cbSize = sizeof hc;
    if (SystemParametersInfo (SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0)) {
        this->contrast = hc.dwFlags & HCF_HIGHCONTRASTON;
    }

    wchar_t filename [MAX_PATH];
    if (GetCurrentThemeName (filename, MAX_PATH, NULL, 0, NULL, 0) == S_OK) {
        if (std::wcsstr (filename, L"AeroLite.msstyles")) {
            this->contrast = true;
        }
    }

    if (this->contrast) {
        this->colorization.active = GetSysColor (COLOR_ACTIVECAPTION);
        this->colorization.inactive = GetSysColor (COLOR_INACTIVECAPTION);
        this->prevalence = true;
    } else
    if (IsWindows10OrGreater ()) {
        this->prevalence = false;
        this->colorization.active = 0xFFFFFF;

        HKEY hKey;
        if (ptrAllowDarkModeForWindow) {
            if (RegOpenKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
                DWORD value = TRUE;
                DWORD size = sizeof value;
                if (RegQueryValueEx (hKey, L"AppsUseLightTheme", NULL, NULL, reinterpret_cast <LPBYTE> (&value), &size) == ERROR_SUCCESS) {
                    this->light = value;
                }
                RegCloseKey (hKey);
            }
        }

        if (RegOpenKeyEx (HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\DWM", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
            DWORD value = TRUE;
            DWORD size = sizeof value;
            RegQueryValueEx (hKey, L"AccentColor", NULL, NULL, reinterpret_cast <LPBYTE> (&this->colorization.accent), &size);

            if (RegQueryValueEx (hKey, L"ColorPrevalence", NULL, NULL, reinterpret_cast <LPBYTE> (&value), &size) == ERROR_SUCCESS) {
                this->prevalence = value;
            }
            if (this->prevalence) {
                this->colorization.active = this->colorization.accent;
            } else {
                if (this->light) {
                    this->colorization.active = 0xFFFFFF;
                } else {
                    this->colorization.active = 0x000000;
                }
            }
            if (RegQueryValueEx (hKey, L"AccentColorInactive", NULL, NULL, reinterpret_cast <LPBYTE> (&this->colorization.inactive), &size) != ERROR_SUCCESS) {
                if (this->light) {
                    this->colorization.inactive = 0xFFFFFF;
                } else {
                    this->colorization.inactive = 0x2B2B2B; // TODO: retrieve from theme, now hardcoded to match Win10 1809
                }
            }
            RegCloseKey (hKey);
        }
    }
}
