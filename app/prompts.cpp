#include "prompts.h"
#include "../common/appapi.h"
#include "data.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

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

bool DeleteListGroupPrompt (HWND hWnd, const std::wstring & list, const std::wstring & group) {
    // LoadString, replace '{x}', 
    // TODO: text 'this Group will be deleted along with the list of channels' 'if you wish to keep the list, merge the Group it into another one'

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
        dlgDeleteList.pszWindowTitle = L"Delete List Group TBD ";
        dlgDeleteList.pszMainIcon = TD_WARNING_ICON;
        dlgDeleteList.pszContent = L"text text text text text text text";
        dlgDeleteList.cButtons = sizeof dlgDeleteListButtons / sizeof dlgDeleteListButtons [0];
        dlgDeleteList.pButtons = dlgDeleteListButtons;
        dlgDeleteList.nDefaultButton = IDOK;

        if (ptrTaskDialogIndirect (&dlgDeleteList, &action, NULL, NULL) != S_OK) {
            // error?
        }
    } else {
        action = MessageBox (hWnd, L"TBD: Group '%s' will be deleted from List '%s' and all inside???", L"Delete List", MB_OKCANCEL | MB_ICONWARNING);
    }

    return action == IDOK;
}

bool DeleteListItemsPrompt (HWND hWnd, std::size_t n) {
    
    // TODO: check 'remember' flag

    int action = IDYES;
    if (ptrTaskDialogIndirect) {
        TASKDIALOG_BUTTON buttons [] = {
            { IDOK, L"Delete" }, // TODO: LoadString
        };

        TASKDIALOGCONFIG dialog;
        std::memset (&dialog, 0, sizeof dialog);

        dialog.cbSize = sizeof dialog;
        dialog.hwndParent = hWnd;
        dialog.hInstance = reinterpret_cast <HMODULE> (&__ImageBase);
        dialog.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
        dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
        dialog.pszWindowTitle = L"Delete selected from List?";
        dialog.pszMainIcon = TD_WARNING_ICON;
        dialog.pszContent = L"text text text text text text text delete all {N} bookmarked (?) items?";
        dialog.cButtons = sizeof buttons / sizeof buttons [0];
        dialog.pButtons = buttons;
        dialog.nDefaultButton = IDYES;
        dialog.pszVerificationText = L"TBD: Remember (revert in settings)";

        BOOL remember = FALSE;
        if (ptrTaskDialogIndirect (&dialog, &action, NULL, &remember) != S_OK) {
            // error?
        }
        if (remember) {
            // save
        }
    } else {
        action = MessageBox (hWnd, L"TBD: {N} items will be deleted. Delete?", L"Delete selected from List?", MB_OKCANCEL | MB_ICONWARNING);
    }

    return action == IDOK;
    
}