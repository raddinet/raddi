#ifndef EDITBOX_H
#define EDITBOX_H

#include <windows.h>
#include <string>

struct EditDialogBoxParameters {
    std::wstring * text;

    POINT ptReference = { 0, 0 };
    UINT idTitle = 0;
    UINT idSubTitle = 0;
    UINT idEditHint = 0;
    UINT idButtonText = 0;

    void (*onUpdate) (void *, const std::wstring &) = nullptr;
    void * onUpdateParameter = nullptr;
};

bool EditDialogBox (HWND hParent, UINT idBase, HWND hReference, POINT offset, std::wstring *);
bool EditDialogBox (HWND hParent, EditDialogBoxParameters * parameters);

#endif
