#include "source.h"
#include <set>

SourceState::SourceState (const wchar_t * path, const wchar_t * nonce) {
    wchar_t temp [32768];
    if (path != nullptr) {
        if (auto length = GetFullPathName (path, sizeof temp / sizeof temp [0] - 1, temp, NULL)) {
            if (temp [length - 1] != L'\\') {
                temp [length + 0] = L'\\';
                temp [length + 1] = L'\0';
            }
            this->handle = CreateFile (temp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_BACKUP_SEMANTICS,
                                       NULL);
            if (this->handle == INVALID_HANDLE_VALUE) {
                this->report (raddi::log::level::error, 3, path);
                path = nullptr;
            }
        } else {
            this->report (raddi::log::level::error, 3, path);
            path = nullptr;
        }
    }

    if (path == nullptr) {
        if (auto length = GetTempPath (MAX_PATH + 1, temp)) {
            if (temp [length - 1] != L'\\') {
                temp [length + 0] = L'\\';
                temp [length + 1] = L'\0';
            }

            std::wcscat (temp, L"RADDI.NET.");
            std::wcscat (temp, nonce);
            std::wcscat (temp, L"\\");

            if (CreateDirectory (temp, NULL)) {
                this->created = true;
            } else {
                if (GetLastError () != ERROR_ALREADY_EXISTS) {
                    this->report (raddi::log::level::error, 4, path);
                    return;
                }
            }
            this->handle = CreateFile (temp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_BACKUP_SEMANTICS,
                                       NULL);
        }
    }
    
    if (this->handle != INVALID_HANDLE_VALUE) {
        this->report (raddi::log::level::event, 1, temp);
        this->path = temp;
    } else {
        this->report (raddi::log::level::error, 5, temp);
    }
}

Source::Source (const wchar_t * path, const wchar_t * nonce)
    : SourceState (path, nonce)
    , Monitor (this->handle) {}

Source::~Source () {
    if (this->created) {
        this->created = false;
        RemoveDirectory (this->path.c_str ());
    }
}

bool Source::process (const std::wstring & file) {
    auto h = CreateFile ((this->path + file).c_str (),
                         GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_DELETE_ON_CLOSE,
                         NULL);
    if (h != INVALID_HANDLE_VALUE) {
        InterlockedIncrement (&this->total);
        this->report (raddi::log::level::event, 4, file);

        DWORD n, nn;
        unsigned char message [sizeof (raddi::entry) + raddi::entry::max_content_size + 1 + 16];

        if (ReadFile (h, message, sizeof message, &n, NULL)) {
            if (n >= sizeof (raddi::entry) + raddi::proof::min_size) {
                if (!this->entry (reinterpret_cast <const raddi::entry *> (message), n)) {
                    this->report (raddi::log::level::error, 2, file);
                }
            } else
                if (n >= sizeof (raddi::command)) {
                    this->command (reinterpret_cast <const raddi::command *> (message), n);
                } else {
                    this->report (raddi::log::level::event, 9, file);
                }

            // destroy traces before deleting
            //  1) overwrite data (could reside within MFT record)
            //  2) extend to max entry size; all deleted files of the same size

            randombytes_buf (message, n);
            SetFilePointer (h, 0, NULL, FILE_BEGIN);
            WriteFile (h, message, n, &nn, NULL);

            randombytes_buf (message, sizeof message);
            SetFilePointer (h, 0, NULL, FILE_BEGIN);
            WriteFile (h, message, sizeof message, &nn, NULL);
            FlushFileBuffers (h);
        }
        CloseHandle (h);
        return true;
    } else
        switch (GetLastError ()) {
            case ERROR_FILE_NOT_FOUND:
                return true;
            case ERROR_SHARING_VIOLATION:
                return false;
            default:
                return this->report (raddi::log::level::error, 1, file);
        }
}
