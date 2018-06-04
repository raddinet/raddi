#ifndef RADDI_DIRECTORY_H
#define RADDI_DIRECTORY_H

#include <windows.h>

// directory
//  - simple filesystem directory search/enumeration abstraction for future porting
//
class directory {
    HANDLE          search;
    WIN32_FIND_DATA found;

public:
    explicit directory (const wchar_t * query);
    ~directory ();

    template <typename Callback>
    bool operator () (Callback callback) {
        if (this->search != INVALID_HANDLE_VALUE) {
            do {
                if (this->found.cFileName [0] == L'.' && this->found.cFileName [1] == L'\0') continue;
                if (this->found.cFileName [0] == L'.' && this->found.cFileName [1] == L'.' && this->found.cFileName [2] == L'\0') continue;

                callback (this->found.cFileName);
            } while (FindNextFile (this->search, &this->found));
            return true;
        } else
            return false;
    }

public:

    // create_result
    //  - one of the potential result of the 'directory::create' function
    //
    enum create_result {
        create_failed = 0,
        created = 1,
        already_exists = 2
    };

    // create
    //  - creates last-level directory of 'path', if possible
    //  - NOTE: the function is platform-dependent, not recursive
    //
    static create_result create (const wchar_t * path) noexcept;

};

#endif
