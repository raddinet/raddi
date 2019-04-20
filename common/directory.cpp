#include "directory.h"

directory::directory (const wchar_t * query) {
    this->search = FindFirstFileEx (query, FindExInfoBasic, &found, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);

    if (this->search == INVALID_HANDLE_VALUE) {
        this->search = FindFirstFileEx (query, FindExInfoStandard, &found, FindExSearchNameMatch, NULL, 0);
    }
}
directory::~directory () {
    if (this->search != INVALID_HANDLE_VALUE) {
        FindClose (this->search);
    }
}

directory::create_result directory::create (const wchar_t * path) noexcept {
    if (CreateDirectory (path, NULL)) {
        return directory::created;
    } else
    if (GetLastError () == ERROR_ALREADY_EXISTS) {

        auto attr = GetFileAttributes (path);
        if ((attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return directory::already_exists;
    }

    return directory::create_failed;
}
