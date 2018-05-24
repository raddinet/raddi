#include "raddi_subscription_set.h"

void raddi::subscription_set::subscribe (const std::wstring & app, const eid & subscription) {
    exclusive guard (this->lock);
    this->data [app].subscribe (subscription);
}

bool raddi::subscription_set::unsubscribe (const std::wstring & app, const eid & subscription) {
    exclusive guard (this->lock);
    try {
        this->data.at (app).unsubscribe (subscription);
        return true;
    } catch (const std::out_of_range &) {
        // not such 'app'
        return false;
    }
}

bool raddi::subscription_set::is_subscribed (const eid * begin, const eid * end) const {
    immutability guard (this->lock);
    for (const auto & subs : this->data) {
        if (subs.second.is_subscribed (begin, end))
            return true;
    }
    return false;
}

bool raddi::subscription_set::load () {
    exclusive guard (this->lock);

    if (CreateDirectory (this->path.c_str (), NULL))
        return true;

    if (GetLastError () == ERROR_ALREADY_EXISTS) {

        // TODO: abstract this to 'directory' class, reuse in 'table'
        //  - having wildcards ar a separate argument for 'list' function

        WIN32_FIND_DATA found;
        auto wildcard = this->path + L"*";
        auto search = FindFirstFileEx (wildcard.c_str (), FindExInfoBasic, &found, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);

        if (search == INVALID_HANDLE_VALUE) {
            search = FindFirstFileEx (wildcard.c_str (), FindExInfoStandard, &found, FindExSearchNameMatch, NULL, 0);
        }
        if (search != INVALID_HANDLE_VALUE) {
            try {
                do {
                    const std::wstring file = found.cFileName;
                    if (file != L"." && file != L"..") {

                        auto full = this->path + file;
                        if (auto n = this->data [file].load (full)) {
                            raddi::log::note (component::database, 16, n - 1, full);
                        } else {
                            raddi::log::error (component::database, 22, full);
                        }
                    }
                } while (FindNextFile (search, &found));
                FindClose (search);
                return true;

            } catch (const std::bad_alloc &) {
                FindClose (search);
                raddi::log::error (component::database, 21);
                return false;
            }
        }
    }

    raddi::log::error (component::database, 22, this->path);
    return false;
}

void raddi::subscription_set::flush () const {
    immutability guard (this->lock);
    for (const auto & [app, sub] : this->data) {
        if (sub.changed) {
            sub.save (this->member (app));
        }
    }
}

std::wstring raddi::subscription_set::member (const std::wstring & app) const {
    return this->path + app;
}
