#include "searchbox.h"
#include "../common/appapi.h"

#include <CommCtrl.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
    ATOM atom = 0;
    LRESULT CALLBACK Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
}

ATOM InitializeSearchBox (HINSTANCE hInstance) {
    WNDCLASSEX wndclass = {
        sizeof (WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW,
        Procedure, 0, 0, hInstance,  NULL, NULL, NULL, NULL, L"SEARCH", NULL
    };
    return (atom = RegisterClassEx (&wndclass));
}

HWND CreateSearchBox (HWND hParent, SearchDialogBoxParameters * parameters) {
    return CreateWindowEx (WS_EX_CLIENTEDGE, (LPCTSTR) (std::intptr_t) atom, L"",
                           WS_VISIBLE | WS_POPUP | WS_BORDER, 0,0,0,0, hParent, NULL,
                           reinterpret_cast <HINSTANCE> (&__ImageBase), parameters);
}

namespace {
    LRESULT CALLBACK Procedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {

            case WM_CREATE:
                // TODO: create title, combobox, cancel button?
                // SetDlgItemText (hWnd, 0x101, parameters->text.c_str ());
                SetProp (hWnd, L"DATA", reinterpret_cast <const CREATESTRUCT *> (lParam)->lpCreateParams);
                break;

            case WM_KILLFOCUS:
                break;
            case WM_ACTIVATE:
                if (wParam == WA_INACTIVE) {
                    // onCancel
                    // DestroyWindow (hWnd);
                }
                break;

            case WM_COMMAND:
                switch (LOWORD (wParam)) {
                    case IDOK:
                    case IDCANCEL:
                        break;

                    case 0x101: // EDIT BOX
                        switch (HIWORD (wParam)) {
                            case EN_CHANGE:
                                if (auto * data = reinterpret_cast <SearchDialogBoxParameters *> (GetProp (hWnd, L"DATA"))) {
                                    if (data->onUpdate) {
                                        try {
                                            data->text = GetDlgItemString (hWnd, 0x101);
                                            data->onUpdate (data->onUpdateParameter, data->text);
                                        } catch (...) {
                                        }
                                    }
                                }
                                break;
                        }
                        break;
                }
                break;
        }
        return DefWindowProc (hWnd, message, wParam, lParam);
    }
}
