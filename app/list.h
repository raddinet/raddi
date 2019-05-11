#ifndef LIST_H
#define LIST_H

#include <windows.h>

#include <string>
#include <array>
#include <map>

#include "../core/raddi.h"

struct WindowPublic;

// User-defined list of channels

class List {
    HWND hWnd;
    UINT index; // `lists`.`id`

public:
    static HWND Create (const WindowPublic * parent, UINT base, UINT index);
    static LRESULT OnNotify (NMHDR *);

private:
    List (HWND hWnd, UINT id);
};


// Virtual List-Views for Channels and Identities lists

class ListVirtual {
public:
    const HWND hWnd;
protected:
    ListVirtual (const WindowPublic * parent, UINT id);
    LRESULT OnNotify (NMHDR *);
};

class ListOfChannels : public ListVirtual {
public:
    ListOfChannels (const WindowPublic * parent, UINT id);

    LRESULT Update (std::size_t n_threads, std::size_t n_channels);
    LRESULT OnNotify (NMHDR *);

private:
    std::map <raddi::eid, int> cache;

    // TODO: Modes:
    //  - show all: obvious
    //  - active in last 2 months: either created, or has threads

};

class ListOfIdentities : public ListVirtual {
public:
    ListOfIdentities (const WindowPublic * parent, UINT id);

    LRESULT Update (std::size_t n);
    LRESULT OnNotify (NMHDR *);

private:
    // TODO: how to cache potentially huge list of iids?
    std::map <raddi::iid, int> cache;

};

#endif
