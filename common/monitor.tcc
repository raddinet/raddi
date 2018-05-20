#ifndef RADDI_MONITOR_TCC
#define RADDI_MONITOR_TCC

#include <windows.h>
#include <set>

template <raddi::component LogProviderComponent>
bool Monitor <LogProviderComponent>::start () {
    return this->active
        || (this->active = this->next () && !this->report (raddi::log::level::event, 2));
}

template <raddi::component LogProviderComponent>
bool Monitor <LogProviderComponent>::stop () {
    if (this->active) {
        this->active = false;
        CancelIo (this->directory);
        return true;
    } else
        return false;
}

template <raddi::component LogProviderComponent>
Monitor <LogProviderComponent>::Monitor (HANDLE directory)
    : directory (directory) {

    if (this->directory != INVALID_HANDLE_VALUE) {
        this->buffer.resize (4096);
        if (!this->await (this->directory)) {
            this->report (raddi::log::level::error, 5, L"^^^");
        }
    }
};

template <raddi::component LogProviderComponent>
Monitor <LogProviderComponent>::Monitor (const std::wstring & path)
    : directory (CreateFile (path.c_str (), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_BACKUP_SEMANTICS,
                             NULL)) {

    if (this->directory != INVALID_HANDLE_VALUE) {
        this->buffer.resize (4096);
        if (!this->await (this->directory)) {
            this->report (raddi::log::level::error, 5, path);
        }
    }
};

template <raddi::component LogProviderComponent>
Monitor <LogProviderComponent>::~Monitor () {
    if (this->directory != INVALID_HANDLE_VALUE) {
        this->stop ();
        CloseHandle (this->directory);
    }
}

template <raddi::component LogProviderComponent>
bool Monitor <LogProviderComponent>::next () {
    this->retry = false;
    return ReadDirectoryChangesW (this->directory, &this->buffer [0], (DWORD) this->buffer.size (),
                                  FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, this, NULL)
        || this->report (raddi::log::level::error, 6);
}

template <raddi::component LogProviderComponent>
void Monitor <LogProviderComponent>::completion (bool success, std::size_t n) {
    if (success) {
        if (this->active) {
            if (n) {
                this->report (raddi::log::level::note, 1, n);
            } else {
                if (this->retry) {
                    this->report (raddi::log::level::note, 2);
                } else {
                    this->report (raddi::log::level::error, 7);
                }
            }

            auto again = 0uL;
            std::set <std::wstring> files; // to merge duplicates and order by time (assuming client honors naming)

            try {
                if (n) {
                    Sleep (50);

                    auto p = reinterpret_cast <FILE_NOTIFY_INFORMATION *> (&this->buffer [0]);
                    do {
                        if (p->Action == FILE_ACTION_ADDED || p->Action == FILE_ACTION_MODIFIED) {
                            files.emplace (p->FileName, p->FileNameLength / sizeof (wchar_t));
                        }
                        p = reinterpret_cast <FILE_NOTIFY_INFORMATION *> (reinterpret_cast <unsigned char *> (p) + p->NextEntryOffset);
                    } while (p->NextEntryOffset);

                } else {
                    WIN32_FIND_DATA file;
                    if (auto search = FindFirstFile ((this->render_directory_path () + L"*").c_str (), &file)) {
                        do {
                            if (!(file.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_REPARSE_POINT))) {
                                files.emplace (file.cFileName);
                            }
                        } while (FindNextFile (search, &file));
                        FindClose (search);
                    }
                }

                for (const auto & file : files) {
                    if (!this->process (file)) {
                        again = 50;
                    }
                }
                
                if (n > 3 * this->buffer.size () / 4) {
                    this->buffer.resize (2 * this->buffer.size ());
                    this->report (raddi::log::level::note, 3, this->buffer.size ());
                }
            } catch (const std::bad_alloc &) {
                again = 8192;
            }

            if (again) {
                Sleep (again); // blocking worker thread!
                this->retry = true;
                this->enqueue ();
            } else {
                this->next ();
            }
        }
    } else {
        // stopped
        this->report (raddi::log::level::event, 3);
    }
}

#endif
