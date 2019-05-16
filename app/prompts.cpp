#include "prompts.h"
#include "appapi.h"
#include "data.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

bool DeleteListPrompt (HWND hWnd, const std::wstring & list) {
    // LoadString, replace 'list', 

    int action = IDOK;
    if (ptrTaskDialogIndirect) {
        TASKDIALOG_BUTTON dlgDeleteListButtons [] = {
            { IDOK, L"Delete" }, // TODO: LoadString
        };

        TASKDIALOGCONFIG dlgDeleteList;
        std::memset (&dlgDeleteList, 0, sizeof dlgDeleteList);

        dlgDeleteList.cbSize = sizeof dlgDeleteList;
        dlgDeleteList.hwndParent = hWnd;
        dlgDeleteList.hInstance = reinterpret_cast <HMODULE> (&__ImageBase);
        dlgDeleteList.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
        dlgDeleteList.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        dlgDeleteList.pszWindowTitle = L"Delete List TBD ";
        dlgDeleteList.pszMainIcon = TD_WARNING_ICON;
        dlgDeleteList.pszContent = L"text text text text text text text";
        dlgDeleteList.cButtons = sizeof dlgDeleteListButtons / sizeof dlgDeleteListButtons [0];
        dlgDeleteList.pButtons = dlgDeleteListButtons;
        dlgDeleteList.nDefaultButton = IDOK;

        if (ptrTaskDialogIndirect (&dlgDeleteList, &action, NULL, NULL) != S_OK) {
            // error?
        }
    } else {
        action = MessageBox (hWnd, L"TBD: List '%s' will be deleted and all inside???", L"Delete List", MB_OKCANCEL | MB_ICONWARNING);
    }

    return action == IDOK;
}

UINT CloseWindowPrompt (HWND hWnd) {

    // TODO: check 'remember' flag

    int action = IDYES;
    if (ptrTaskDialogIndirect) {
        TASKDIALOG_BUTTON dlgCloseWindowButtons [] = {
            { IDYES, L"Stash\nReopen from menu and whatever" },
            { IDNO, L"Close\nAll tabs and window begone" },
        };

        TASKDIALOGCONFIG dlgCloseWindow;
        std::memset (&dlgCloseWindow, 0, sizeof dlgCloseWindow);

        dlgCloseWindow.cbSize = sizeof dlgCloseWindow;
        dlgCloseWindow.hwndParent = hWnd;
        dlgCloseWindow.hInstance = reinterpret_cast <HMODULE> (&__ImageBase);
        dlgCloseWindow.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
        dlgCloseWindow.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        dlgCloseWindow.pszWindowTitle = L"close window TBD TBD TBD TBD TBD TBD ";
        dlgCloseWindow.pszMainIcon = TD_WARNING_ICON;
        dlgCloseWindow.pszContent = L"text text text text text text text";
        dlgCloseWindow.cButtons = sizeof dlgCloseWindowButtons / sizeof dlgCloseWindowButtons [0];
        dlgCloseWindow.pButtons = dlgCloseWindowButtons;
        dlgCloseWindow.nDefaultButton = IDYES;
        dlgCloseWindow.pszVerificationText = L"TBD: Remember (revert in settings)";

        BOOL remember = FALSE;
        if (ptrTaskDialogIndirect (&dlgCloseWindow, &action, NULL, &remember) != S_OK) {
            // error?
        }
        if (remember) {
            // save
        }
    } else {
        action = MessageBox (hWnd, L"TBD: Stash???", L"Close Window", MB_YESNOCANCEL | MB_ICONQUESTION);
    }

    return action;
}
