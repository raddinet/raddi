#include "raddi_database_peerset.h"
#include "../common/file.h"

void raddi::db::peerset::load (const std::wstring & path, int family, int level) {
    wchar_t name [8];
    _snwprintf (name, sizeof name / sizeof name [0], L"\\%02xL%d", family, level);

    this->paths [family] = path + name;

    file f;
    if (f.open (this->paths [family],
                file::mode::always, file::access::read, file::share::read, file::buffer::sequential)) {
        address a;
        std::uint16_t s;
        std::size_t n = 0;

        a.family = family;
        while (f.read (a.data (), address::size (family)) && f.read (&s, sizeof s)) {
            this->addresses [a] = s;
            ++n;
        }

        this->report (log::level::note, 0x20, &name[1], address::name (family), n, this->addresses.size ());
    } else
        this->report (log::level::error, 0x22, this->paths [family], address::name (family));
}

void raddi::db::peerset::save (int family) const {
    file f;
    if (f.create (this->paths.at (family))) {
        std::size_t written = 0;

        for (const auto & [address, assessment] : this->addresses) {
            if (address.accessible (address::validation::allow_null_port)) {
                if (address.family == family) {
                    if (f.write (address.data (), address.size ()) && f.write (assessment)) {
                        ++written;
                    } else {
                        this->report (log::level::error, 0x24, this->paths.at (family), address::name (family));
                        break;
                    }
                }
            }
        }
        this->report (log::level::note, 0x21, this->paths.at (family), address::name (family), written);
    } else
        this->report (log::level::error, 0x23, this->paths.at (family), address::name (family));
}

void raddi::db::peerset::save () const {
    immutability guard (this->lock);

    if (this->ipv4changed) {
        this->ipv4changed = false;
        this->save (AF_INET);
    }
    if (this->ipv6changed) {
        this->ipv6changed = false;
        this->save (AF_INET6);
    }
}

std::uint32_t raddi::db::peerset::adjust (const address & a, std::int16_t adj) {
    immutability guard (this->lock);
    auto i = this->addresses.find (a);
    if (i != this->addresses.end ()) {
        if (adj) {
            auto updated = (int) i->second + (int) adj;

            if (updated < 0)
                updated = 0;
            if (updated > 0xFF)
                updated = 0xFF;

            i->second = (std::uint16_t) updated;

            if (a.accessible (address::validation::allow_null_port)) {
                switch (a.family) {
                    case AF_INET:
                        this->ipv4changed = true;
                        break;
                    case AF_INET6:
                        this->ipv6changed = true;
                        break;
                }
            }
        }
        return i->second;
    } else
        return 0;
}

raddi::address
raddi::db::peerset::select (std::size_t random_value, std::uint16_t * assessment) {
    immutability guard (this->lock);
    auto size = this->addresses.size ();
    if (size) {
        auto i = this->addresses.begin ();
        std::advance (i, random_value % size);
        if (assessment) {
            *assessment = i->second;
        }
        return i->first;
    } else {
        // assert (false);
        return address ();
    }
}

void raddi::db::peerset::prune (std::uint16_t threshold) {
    exclusive guard (this->lock);

    auto i = this->addresses.begin ();
    auto e = this->addresses.end ();
    while (i != e) {
        if (threshold >= i->second) {
            i = this->addresses.erase (i);
            e = this->addresses.end ();
        } else
            ++i;
    }
}

bool raddi::db::peerset::empty () const {
    immutability guard (this->lock);
    return this->addresses.empty ();
}
bool raddi::db::peerset::count (const raddi::address & a) const {
    immutability guard (this->lock);
    return this->addresses.count (a);
}
bool raddi::db::peerset::count_ip (const raddi::address & a) const {
    auto lower = a;
    auto upper = a;

    lower.port = 0;
    upper.port = 65535;

    immutability guard (this->lock);
    auto i = this->addresses.lower_bound (lower);
    return i != this->addresses.end ()
        && i->first <= upper;
}

void raddi::db::peerset::erase (const address & a) {
    exclusive guard (this->lock);
    if (a.port == 0) {
        auto lower = a;
        auto upper = a;

        lower.port = 0;
        upper.port = 65535;

        while (true) {
            auto i = this->addresses.lower_bound (lower);
            if (i != this->addresses.end () && i->first <= upper) {
                this->addresses.erase (i);
                switch (a.family) {
                    case AF_INET:
                        this->ipv4changed = true;
                        break;
                    case AF_INET6:
                        this->ipv6changed = true;
                        break;
                }
            } else
                return;
        }
    } else {
        this->addresses.erase (a);
        switch (a.family) {
            case AF_INET:
                this->ipv4changed = true;
                break;
            case AF_INET6:
                this->ipv6changed = true;
                break;
        }
    }
}
void raddi::db::peerset::insert (const address & a, std::uint16_t s) {
    exclusive guard (this->lock);
    if (this->addresses.emplace (a, s).second && a.accessible (address::validation::allow_null_port)) {
        switch (a.family) {
            case AF_INET:
                this->ipv4changed = true;
                break;
            case AF_INET6:
                this->ipv6changed = true;
                break;
        }
    }
}
