#ifndef SEARCHBOX_H
#define SEARCHBOX_H

#include <windows.h>
#include <string>

struct SearchDialogBoxParameters {
    std::wstring text;
    POINT pt = { 0, 0 };
    
    /*UINT idTitle = 0;
    UINT idSubTitle = 0;
    UINT idEditHint = 0;
    UINT idButtonText = 0;
    */
    void (*onUpdate) (void *, const std::wstring &) = nullptr;
    void * onUpdateParameter = nullptr;

    void (*onCancel) (void *) = nullptr;
    void * onCancelParameter = nullptr;
};

ATOM InitializeSearchBox (HINSTANCE hInstance);
HWND CreateSearchBox (HWND hParent, SearchDialogBoxParameters * parameters);
void CancelSearchBox (HWND hBox);

#endif
