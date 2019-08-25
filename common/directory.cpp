#include "directory.h"
#include <cwchar>

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

namespace {
    bool step (const wchar_t *);
}

directory::create_result directory::create_full_path (const wchar_t * path) noexcept {
    wchar_t full [32768u];
    if (GetFullPathName (path, sizeof full / sizeof full [0], full, nullptr)) {

        if (step (full))
            return directory::created;

        if (GetLastError () == ERROR_ALREADY_EXISTS) {

            auto attr = GetFileAttributes (full);
            if ((attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY))
                return directory::already_exists;
        }
    }
    return directory::create_failed;
}

namespace {
    bool step (const wchar_t * path) {

        if (CreateDirectory (path, NULL))
            return true;

        if (GetLastError () == ERROR_PATH_NOT_FOUND) {
            if (wchar_t * slash = (wchar_t *) std::wcsrchr (path, L'\\')) {

                *slash = L'\0';
                if (step (path)) {
                    *slash = L'\\';

                    if (CreateDirectory (path, NULL)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }
}
