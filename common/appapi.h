#ifndef APPAPI_H
#define APPAPI_H

#include <windows.h>
#include <uxtheme.h>
#include <dwmapi.h>

#include <string_view>
#include <string>

#include "../common/platform.h"

void AppApiInitialize (); // loads ptrAbcXyz functions below

UINT GetDPI (HWND hWnd);
bool AreDpiApisScaled (HWND hWnd);
HICON LoadBestIcon (HMODULE hModule, LPCWSTR resource, SIZE size);
HBRUSH CreateSolidBrushEx (COLORREF color);
HBRUSH CreateSolidBrushEx (COLORREF color, unsigned char alpha);
HPEN CreatePenEx (DWORD style, DWORD width, COLORREF color);
HPEN CreatePenEx (DWORD style, DWORD width, COLORREF color, unsigned char alpha);
RECT FixWindowCoordinates (int x, int y, int w, int h);
bool IsLastWindow (HWND hWnd);
bool IsColorDark (COLORREF color);
bool IsWindowClass (HWND hWnd, std::wstring_view name);

typedef struct tagNMHDRCOOKIE {
    HWND      hwndFrom;
    UINT_PTR  idFrom : (8 * sizeof (UINT_PTR) - 1);
    UINT_PTR  cookie : 1; // since idFrom from Comctl32 are 16-bit, we have upper 16..48 bits to play with
    UINT      code;
    // UINT      cookie; // it's not safe to reuse the padding, that's available only on 64-bit
} NMHDRCOOKIE;

struct NMOUTOFMEMORY {
    NMHDRCOOKIE hdr;
    // TODO: global app custom extension (NMOUTOFMEMORY) to pass where actually this happened

    static_assert (sizeof (NMHDR) == sizeof (NMHDRCOOKIE));
};

LRESULT ReportOutOfMemory (HWND hControl);
LRESULT ReportOutOfMemory (HWND hParent, UINT control);
LRESULT ReportOutOfMemory (HWND hParent, HWND hControl, UINT idControl);

std::wstring GetWindowString (HWND hWnd);
std::wstring GetDlgItemString (HWND hWnd, UINT id);

typedef enum CompositionAttribute {
    WCA_NCRENDERING_ENABLED = 1,
    WCA_NCRENDERING_POLICY = 2,
    WCA_CAPTION_BUTTON_BOUNDS = 5,
    WCA_ACCENT_POLICY = 19,
    WCA_USEDARKMODECOLORS = 26, // build 18875+
} CompositionAttribute;

#ifndef DWMWA_MICA
#define DWMWA_MICA ((DWMWINDOWATTRIBUTE) 1029)
#endif

struct CompositionAttributeData {
    CompositionAttribute attribute;
    PVOID data;
    ULONG size;
};

struct AccentPolicy {
    DWORD state;
    DWORD flags;
    COLORREF gradient;
    DWORD animation;
};

struct DrawCompositedTextOptions {
    HTHEME  theme = NULL;
    HFONT   font = NULL;
    COLORREF color = 0x000000;
    UINT    glow = 0;
    //struct {
    //    LONG size = 0;
    //    POINT offset = { 0, 0 };
    //    COLORREF color = 0x000000;
    //    UCHAR alpha;
    //} shadow;
    // UCHAR alpha
};

HRESULT DrawCompositedTextDIB (HDC, HTHEME, HFONT, LPCWSTR, int, DWORD, RECT, COLORREF, UINT);
HRESULT DrawCompositedText (HDC, std::wstring_view, DWORD, RECT, const DrawCompositedTextOptions * = NULL);
HRESULT BufferedPaintPremultiply (HPAINTBUFFER hBuffer, const RECT & r, UCHAR alpha, UCHAR saturation = 255);
LRESULT CALLBACK AlphaSubclassProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);

struct Design {
    bool composited = true; // DWM composition is enabled
    bool nice = false; // DWM or theme on Vista/7
    bool light = true; // true = system color, false = artificially dark
    bool prevalence = true;
    bool contrast = false;

    bool may_need_fix_alpha = true; // TODO: Vista, 7 or 11+
    bool fix_alpha = false; // need to fix alpha on glass

    struct {
        bool outline = true; // Windows 11, outline only, instead of fully colorized titlebar

        // backdrop on Windows 11 22000 will attempt to render with acrylic backdrop
        // backdrop on Windows 11 22500+
        //  - DWMSBT_AUTO - will not render background, fully transparent, not supported by our background renderer
        //  - DWMSBT_MAINWINDOW - mica background
        //  - DWMSBT_TABBEDWINDOW - strong mica background
        //  - DWMSBT_TRANSIENTWINDOW - acrylic backdrop, not currently supported by our background renderer
        //
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TABBEDWINDOW;
        DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DEFAULT; // Windows 11 rounded corners mode
    } override;

    struct {
        DWORD accent = 0xFFFFFF;
        DWORD active = 0xFFFFFF;
        DWORD inactive = 0xFFFFFF;

        DWORD text = 0;
        DWORD background = 0xFFFFFF;
    } colorization;

    UINT colorset = 0;
    void update ();
};

struct Cursors {
    HCURSOR wait = NULL;
    HCURSOR arrow = NULL;
    HCURSOR working = NULL;
    HCURSOR vertical = NULL;
    HCURSOR horizontal = NULL;

    void update ();
};

template <unsigned int N>
int TranslateAccelerators (HWND hWnd, const HACCEL (&table) [N], LPMSG msg);

template <unsigned int N>
int TranslateAccelerators (HWND hWnd, const HACCEL (&table) [N], LPMSG msg) {
    for (auto accelerator : table) {
        if (TranslateAccelerator (hWnd, accelerator, msg))
            return true;
    }
    return false;
}

extern HRESULT (WINAPI * ptrBufferedPaintInit) ();
extern HRESULT (WINAPI * ptrBufferedPaintSetAlpha) (HPAINTBUFFER, const RECT *, BYTE);
extern HPAINTBUFFER (WINAPI * ptrBeginBufferedPaint) (HDC, const RECT *, BP_BUFFERFORMAT, const BP_PAINTPARAMS *, HDC *);
extern HRESULT (WINAPI * ptrEndBufferedPaint) (HPAINTBUFFER, BOOL);
extern HDC (WINAPI * ptrGetBufferedPaintDC) (HPAINTBUFFER);
extern HRESULT (WINAPI * ptrGetBufferedPaintBits) (HPAINTBUFFER, RGBQUAD **, int *);
extern HRESULT (WINAPI * ptrDrawThemeTextEx) (HTHEME, HDC, int, int, LPCWSTR, int, DWORD, LPRECT, const DTTOPTS *);

extern BOOL (WINAPI * ptrAllowDarkModeForApp) (BOOL);
extern BOOL (WINAPI * ptrAllowDarkModeForWindow) (HWND, BOOL);
extern void (WINAPI * ptrFlushMenuThemes) ();
extern void (WINAPI * ptrRefreshImmersiveColorPolicyState) ();

extern BOOL (WINAPI * ptrDwmDefWindowProc) (HWND, UINT, WPARAM, LPARAM, LRESULT *);
extern HRESULT (WINAPI * ptrDwmGetWindowAttribute) (HWND, DWORD, PVOID, DWORD);
extern HRESULT (WINAPI * ptrDwmSetWindowAttribute) (HWND, DWORD, LPCVOID, DWORD);
extern HRESULT (WINAPI * ptrDwmExtendFrameIntoClientArea) (HWND, const MARGINS *);
extern HRESULT (WINAPI * ptrDwmGetColorizationColor) (DWORD *, BOOL *);
extern HRESULT (WINAPI * ptrDwmIsCompositionEnabled) (BOOL *);
extern HRESULT (WINAPI * ptrDwmEnableBlurBehindWindow) (HWND, const DWM_BLURBEHIND *);

extern BOOL (WINAPI * ptrEnableNonClientDpiScaling) (HWND);
extern BOOL (WINAPI * ptrChangeWindowMessageFilter) (UINT, DWORD);
extern UINT (WINAPI * pfnGetDpiForSystem) ();
extern UINT (WINAPI * pfnGetDpiForWindow) (HWND);
extern int (WINAPI * ptrGetSystemMetricsForDpi) (int, UINT);
extern DPI_AWARENESS_CONTEXT (WINAPI * ptrGetWindowDpiAwarenessContext) (HWND);
extern BOOL (WINAPI * ptrAreDpiAwarenessContextsEqual) (DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
extern BOOL (WINAPI * ptrSetWindowCompositionAttribute) (HWND, CompositionAttributeData *);

extern HRESULT (WINAPI * ptrLoadIconWithScaleDown) (HINSTANCE, PCWSTR, int, int, HICON *);
extern HRESULT (WINAPI * ptrTaskDialogIndirect) (const TASKDIALOGCONFIG *, int * button, int * radio, BOOL * check);

#endif
