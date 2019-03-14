#ifndef APPAPI_H
#define APPAPI_H

#include <windows.h>
#include <uxtheme.h>

#include "../common/platform.h"

void AppApiInitialize (); // loads ptrAbcXyz functions below

UINT GetDPI (HWND hWnd);
bool AreDpiApisScaled (HWND hWnd);
HICON LoadBestIcon (HMODULE hModule, LPCWSTR resource, SIZE size);
HBRUSH CreateSolidBrushEx (COLORREF color);
HBRUSH CreateSolidBrushEx (COLORREF color, unsigned char alpha);
RECT FixWindowCoordinates (int x, int y, int w, int h);
bool IsLastWindow (HWND hWnd);
bool IsColorDark (COLORREF color);

HRESULT DrawCompositedText (HDC, HTHEME, HFONT, LPCWSTR, int, DWORD, RECT, COLORREF, UINT);
HRESULT BufferedPaintPremultiply (HPAINTBUFFER hBuffer, const RECT & r, UCHAR alpha, UCHAR saturation = 255);

struct Design {
    bool composited = true; // DWM composition is enabled
    bool nice = false; // DWM or theme on Vista/7
    bool light = true;
    bool prevalence = true;
    bool contrast = false;
    bool alpha = false; // need to fix alpha on glass

    struct {
        DWORD accent = 0xFFFFFF;
        DWORD active = 0xFFFFFF;
        DWORD inactive = 0xFFFFFF;
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

extern BOOL (WINAPI * ptrEnableNonClientDpiScaling) (HWND);
extern BOOL (WINAPI * ptrChangeWindowMessageFilter) (UINT, DWORD);
extern UINT (WINAPI * pfnGetDpiForSystem) ();
extern UINT (WINAPI * pfnGetDpiForWindow) (HWND);
extern int (WINAPI * ptrGetSystemMetricsForDpi) (int, UINT);
extern DPI_AWARENESS_CONTEXT (WINAPI * ptrGetWindowDpiAwarenessContext) (HWND);
extern BOOL (WINAPI * ptrAreDpiAwarenessContextsEqual) (DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);

extern HRESULT (WINAPI * ptrLoadIconWithScaleDown) (HINSTANCE, PCWSTR, int, int, HICON *);
extern HRESULT (WINAPI * ptrTaskDialogIndirect) (const TASKDIALOGCONFIG *, int * button, int * radio, BOOL * check);

#endif
