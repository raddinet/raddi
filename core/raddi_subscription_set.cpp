#include "raddi_subscription_set.h"
#include "../common/directory.h"

void raddi::subscription_set::subscribe (const uuid & app, const eid & subscription) {
    exclusive guard (this->lock);
    this->data [app].subscribe (subscription);
}

bool raddi::subscription_set::unsubscribe (const uuid & app, const eid & subscription) {
    exclusive guard (this->lock);
    try {
        return this->data.at (app).unsubscribe (subscription);
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

    switch (directory::create (this->path.c_str ())) {

        case directory::created:
            return true;

        default:
        case directory::create_failed:
            raddi::log::error (component::database, 22, this->path);
            return false;

        case directory::already_exists:
            try {
                auto callback = [this] (const wchar_t * filename) {
                    uuid app;
                    auto full = this->path + filename;
                    
                    if (app.parse (filename)) {
                        if (auto n = this->data [app].load (full)) {
                            raddi::log::note (component::database, 16, n - 1, full);
                        } else {
                            raddi::log::error (component::database, 22, full);
                        }
                    } else {
                        raddi::log::error (component::database, 25, full);
                    }
                };
                return directory ((this->path + L"*").c_str ()) (callback) 
                    || raddi::log::error (component::database, 22, this->path);

            } catch (const std::bad_alloc &) {
                raddi::log::error (component::database, 21);
                return false;
            }
    }
}

void raddi::subscription_set::flush () const {
    immutability guard (this->lock);
    for (const auto & [app, sub] : this->data) {
        if (sub.changed) {
            sub.save (this->member (app));
        }
    }
}

std::wstring raddi::subscription_set::member (const uuid & app) const {
    return this->path + app.c_wstr ();
}
