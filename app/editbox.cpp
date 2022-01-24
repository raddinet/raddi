#include "editbox.h"
#include "../common/appapi.h"
#include <CommCtrl.h>
#include <VersionHelpers.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
    INT_PTR OnInitDialog (HWND hWnd, EditDialogBoxParameters * parameters) {
        if (winver < 6) {
            // TODO: use Segoe UI Variable on 22000+
            // TODO: instead consider making two templates, one DS_SHELLFONT,8,MS Whatever and other 9, Segoe UI
            EnumChildWindows (hWnd,
                              [](HWND hCtrl, LPARAM lParam) -> BOOL {
                                  SendMessage (hCtrl, WM_SETFONT, (WPARAM) GetStockObject (DEFAULT_GUI_FONT), 1);
                                  return TRUE;
                              }, 0);
        }

        if ((parameters->ptReference.x != 0) && (parameters->ptReference.y != 0)) {
            RECT r;
            if (GetWindowRect (hWnd, &r)) {
                MoveWindow (hWnd,
                            parameters->ptReference.x, parameters->ptReference.y,
                            r.right - r.left, r.bottom - r.top, FALSE);
            }
        }

        if (!parameters->text->empty ()) {
            SetDlgItemText (hWnd, 0x101, parameters->text->c_str ());
        }
        if (parameters->idEditHint || parameters->idTitle || parameters->idSubTitle || parameters->idButtonText) {
            const auto size = 65536;
            const auto temp = new (std::nothrow) wchar_t [size];

            if (temp) {
                if (parameters->idEditHint) {
                    if (LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase),
                                    parameters->idEditHint, temp, size)) {
                        SendDlgItemMessage (hWnd, 0x101, EM_SETCUEBANNER, TRUE, (LPARAM) temp);
                    }
                }
                if (parameters->idTitle) {
                    if (LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase),
                                    parameters->idTitle, temp, size)) {
                        SetWindowText (hWnd, temp);
                    }
                }
                if (parameters->idSubTitle) {
                    if (LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase),
                                    parameters->idSubTitle, temp, size)) {
                        SetDlgItemText (hWnd, 0x100, temp);
                    }
                }
                if (parameters->idButtonText) {
                    if (LoadString (reinterpret_cast <HINSTANCE> (&__ImageBase),
                                    parameters->idButtonText, temp, size)) {
                        SetDlgItemText (hWnd, IDOK, temp);
                    }
                }
                delete [] temp;
            }
        }

        SetProp (hWnd, L"DATA", parameters);
        return TRUE;
    }

    INT_PTR CALLBACK EditDialogBoxProcedure (HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_INITDIALOG:
                return OnInitDialog (hWnd, reinterpret_cast <EditDialogBoxParameters *> (lParam));

            case WM_COMMAND:
                switch (LOWORD (wParam)) {
                    case IDOK:
                    case IDCANCEL:
                        if (auto * data = reinterpret_cast <EditDialogBoxParameters *> (GetProp (hWnd, L"DATA"))) {
                            try {
                                *data->text = GetDlgItemString (hWnd, 0x101);
                            } catch (const std::bad_alloc &) {
                                MessageBeep (MB_ICONERROR);
                                wParam = IDABORT;
                            }
                        }
                        EndDialog (hWnd, LOWORD (wParam) == IDOK);
                        break;

                    case 0x101: // EDIT BOX
                        switch (HIWORD (wParam)) {
                            case EN_CHANGE:
                                if (auto * data = reinterpret_cast <EditDialogBoxParameters *> (GetProp (hWnd, L"DATA"))) {
                                    if (data->onUpdate) {
                                        try {
                                            *data->text = GetDlgItemString (hWnd, 0x101);
                                            data->onUpdate (data->onUpdateParameter, *data->text);
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

        return FALSE;
    }
}

bool EditDialogBox (HWND hParent, UINT idBase, HWND hReference, POINT offset, std::wstring * text) {
    EditDialogBoxParameters parameters;

    parameters.text = text;
    parameters.idTitle = idBase;
    parameters.idSubTitle = idBase + 1;
    parameters.idButtonText = idBase + 2;

    if (hReference) {
        RECT r;
        if (GetWindowRect (hReference, &r)) {
            parameters.ptReference.x = r.left + offset.x;
            parameters.ptReference.y = r.bottom + offset.y;
        }
    } else {
        parameters.ptReference = offset;
    }

    if (EditDialogBox (hParent, &parameters)) {
        text = parameters.text;
        return true;
    } else
        return false;
}

bool EditDialogBox (HWND hParent, EditDialogBoxParameters * parameters) {
    return DialogBoxParam (reinterpret_cast <HINSTANCE> (&__ImageBase),
                           MAKEINTRESOURCE (1), hParent, EditDialogBoxProcedure,
                           reinterpret_cast <LPARAM> (parameters));
}
