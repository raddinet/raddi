#include "raddi_noticed.h"
#include "../common/directory.h"
#include "../common/file.h"
#include "../common/log.h"
#include <algorithm>

bool raddi::noticed::insert (const raddi::eid & id) {
    exclusive guard (this->lock);
    if (this->data [id.timestamp].insert (id.identity).second) {
        this->changed.insert (id.timestamp);
        return true;
    } else
        return false;
}

void raddi::noticed::clean (std::uint32_t age) {
    exclusive guard (this->lock);

    auto i = this->data.begin ();
    const auto e = this->data.end ();
    const auto threshold = raddi::now () - age;

    while (i != e) {
        if (raddi::older (i->first, threshold)) {
            i = this->data.erase (i);
        } else {
            ++i;
        }
    }
}
bool raddi::noticed::count (const raddi::eid & id) const {
    immutability guard (this->lock);

    auto i = this->data.find (id.timestamp);
    if (i != this->data.end ())
        return i->second.count (id.identity);
    else
        return false;
}

std::size_t raddi::noticed::size () const {
    immutability guard (this->lock);
    std::size_t n = 0;
    for (const auto & sub : this->data) {
        n += sub.second.size ();
    }
    return n;
}

bool raddi::noticed::parse (const wchar_t * string, std::uint32_t * output) {
    wchar_t * end = nullptr;
    *output = std::wcstoul (string, &end, 16);
    
    return (end - string) == 8;
}

bool raddi::noticed::load (const std::wstring & path) {
    exclusive guard (this->lock);

    switch (directory::create (path.c_str ())) {

        case directory::created:
            return true;

        default:
        case directory::create_failed:
            raddi::log::error (component::database, 22, path);
            return false;

        case directory::already_exists:
            try {
                auto callback = [path, this] (const wchar_t * filename) {

                    std::uint32_t key;
                    if (raddi::noticed::parse (filename, &key)) {

                        auto full = path + filename;
                        auto & data = this->data [key];

                        file f;
                        if (f.open (full, file::mode::open, file::access::read, file::share::read, file::buffer::sequential)) {
                            iid item;
                            while (f.read (item)) {
                                data.insert (item);
                            }
                        } else {
                            raddi::log::error (component::database, 22, full);
                        }
                    }
                };
                return directory ((path + L"*").c_str ()) (callback)
                    || raddi::log::error (component::database, 22, path);

            } catch (const std::bad_alloc &) {
                raddi::log::error (component::database, 21);
                return false;
            }
    }
}

void raddi::noticed::save (const std::wstring & path) const {
    immutability guard (this->lock);

    // enum files in directory and erase those not in data

    auto unlinker = [this, path] (const wchar_t * filename) {
        std::uint32_t key;
        if (raddi::noticed::parse (filename, &key)) {
            if (!this->data.count (key)) {
                file::unlink (path + filename);
            }
        }
    };
    directory ((path + L"*").c_str ()) (unlinker);
    
    // write all data

    for (const auto & [key, iids] : this->data) {
        if (this->changed.count (key)) {

            wchar_t k [9];
            std::swprintf (k, sizeof k / sizeof k [0], L"%08x", key);

            file f;
            if (f.create (path + k)) {
                for (const auto & iid : iids) {
                    f.write (iid);
                }
                this->changed.erase (key);
            }
        }
    }
}
