#include <windows.h>

#include <algorithm>
#include <cstring>

#include "raddi_database.h"
#include "raddi_database_row.h"
#include "raddi_database_shard.h"
#include "raddi_database_table.h"
#include "raddi_database_peerset.h"

#include "raddi_timestamp.h"
#include "raddi_channel.h"
#include "raddi_identity.h"
#include "raddi_consensus.h"

#include "../common/log.h"
#include "../common/lock.h"
#include "../common/directory.h"

#include "sodium.h"

raddi::db::db (file::access mode, const std::wstring & path)
    : mode (mode)
    , path (path)
    , data (new table <row> (L"data", *this))
    , threads (new table <trow> (L"threads", *this))
    , channels (new table <crow> (L"channels", *this))
    , identities (new table <irow> (L"identities", *this)) {

    for (auto i = 0; i != levels; ++i) {
        this->peers [i] .reset (new peerset ((level) i));
    }

    if (directory::create (path.c_str ())) {
        if (this->lock.open (path + L"\\.lock", file::mode::always,
                             mode, (mode == file::access::read) ? file::share::full : file::share::read,
                             file::buffer::none)) {

            this->report (raddi::log::level::event, 1, path);

            if (mode == file::access::read) {
                this->data->start ();
                this->threads->start ();
                this->channels->start ();
                this->identities->start ();
            }

            this->data->reload ();
            this->threads->reload ();
            this->channels->reload ();
            this->identities->reload ();
        } else {
            this->report (log::level::error, 2, path);
        }
    } else {
        this->report (log::level::error, 1, path);
    }
}

raddi::db::~db () {
    // only here so the unique_ptr would work
}

raddi::db::statistics raddi::db::stats () const {
    raddi::db::statistics s;
    s += this->data->stats ();
    s += this->threads->stats ();
    s += this->channels->stats ();
    s += this->identities->stats ();
    return s;
}

raddi::db::assessment raddi::db::assess (const void * data, std::size_t size, root * top) {
    const auto entry = static_cast <const raddi::entry *> (data);
    const auto type = entry->is_announcement ();

    struct : public raddi::identity {
        std::uint8_t name [raddi::identity::max_description_size];
    } author;

    std::size_t author_size;

    // find identity, validate signature, on failure add negative mark to the connection

    switch (type) {
        case raddi::entry::new_identity_announcement:
            if (!static_cast <const raddi::identity *> (entry)->verify (size)) {

                this->report (log::level::data, 2, entry->id.serialize ());
                return raddi::db::rejected;
            }
            break;

        case raddi::entry::new_channel_announcement:

            // find announcement of author of this entry

            if (!this->identities->get (entry->id.identity, &author, &author_size)) {

                this->report (log::level::data, 5, entry->id.serialize ());
                return raddi::db::rejected;
            }

            // verify it's signed by that author

            if (!entry->verify (size, &author, author_size, author.public_key)) {

                this->report (log::level::data, 6, entry->id.serialize ());
                return raddi::db::rejected;
            }
            break;

        case raddi::entry::not_an_announcement:

            // find announcement of author of this entry
            //  - reading only content (need public_key), header is not valid here

            if (!this->identities->get (entry->id.identity, read::content,
                                        &author, nullptr, sizeof (identity::public_key))) {

                this->report (log::level::data, 5, entry->id.serialize ());
                return raddi::db::rejected;
            }

            // find parent entry
            //  - complete parent entry is necessary to verify signature of the new one

            struct {
                std::size_t size;

                struct : public raddi::entry {
                    std::uint8_t description [raddi::entry::max_content_size];
                } data;
            } parent;

            if (!this->get (entry->parent, &parent.data, &parent.size)) {

                this->report (log::level::note, 7, entry->id.serialize ());
                return raddi::db::detached;
            }
            if (!entry->verify (size, &parent.data, parent.size, author.public_key)) {

                this->report (log::level::data, 6, entry->id.serialize ());
                return raddi::db::rejected;
            }
            break;
    }

    switch (type) {
        case raddi::entry::new_identity_announcement:
        case raddi::entry::new_channel_announcement:

            // sanitize, even if 'top' is ignored further in proper insertion (?row.classify)
            std::memset (top, 0, sizeof *top);
            return raddi::db::required;

        case raddi::entry::not_an_announcement:
            raddi::db::row r;

            if (this->channels->get (entry->parent)
                    || ((entry->parent.timestamp == entry->parent.identity.timestamp) && this->identities->get (entry->parent.identity))) {

                // parent is normal channel or identity channel: entry is a thread, either in a normal channel or in an identity channel
                top->thread = entry->id;
                top->channel = entry->parent;

            } else
            if (this->threads->get (entry->parent, &r)) {

                // parent is thread: entry is a top level comment within a thread (or vote on the thread)
                top->channel = r.parent;
                top->thread = entry->parent;

            } else
            if (this->data->get (entry->parent, &r)) {

                // parent is normal entry: nested comment, vote or stuff
                *top = r.top ();

            } else {
                return raddi::db::rejected; // unreachable branch
            }
            return raddi::db::classify;
    }
    return raddi::db::rejected; // unreachable
}

bool raddi::db::insert (const void * data, std::size_t size, const root & top, bool & exists) {
    auto entry = static_cast <const raddi::entry *> (data);

    // when inserting to table, size is always less or equal to (0xFFFF - 16) (AES overhead)
    // and gets reduced by 160 bytes (entry header) thus allowing us to add up to 176 bytes
    // of additional data should that be neccessary

    switch (entry->is_announcement ()) {
        case raddi::entry::new_identity_announcement:
            return this->identities->insert (data, size, top, exists);
        case raddi::entry::new_channel_announcement:
            return this->channels->insert (data, size, top, exists);

        case raddi::entry::not_an_announcement:
            if ((entry->id == top.thread) && (entry->parent == top.channel)) {
                return this->threads->insert (data, size, top, exists);
            }
    }
    return this->data->insert (data, size, top, exists);
}

bool raddi::db::get (const raddi::eid & entry, void * buffer, std::size_t * length) const {
    if (entry.identity.timestamp != entry.timestamp)
        return this->data->get (entry, buffer, length)
            || this->threads->get (entry, buffer, length)
            || this->channels->get (entry, buffer, length);
    else
        return this->identities->get (entry.identity, buffer, length);
}

void raddi::db::flush () {
    this->identities->flush ();
    this->channels->flush ();
    this->threads->flush ();
    this->data->flush ();
}

void raddi::db::optimize (bool strong) {
    std::size_t optimized = 0;
    std::size_t pruned = 0;
    
    if (this->settings.shard_trimming_threshold) {
        const auto threshold = raddi::now () - this->settings.shard_trimming_threshold;

        optimized += this->data->optimize (threshold);
        optimized += this->threads->optimize (threshold);
        optimized += this->channels->optimize (threshold);
        optimized += this->identities->optimize (threshold);
    }

    const auto limit = strong ? this->settings.minimum_active_shards
                              : this->settings.maximum_active_shards;
    if (limit) {
        auto active = this->stats ().shards.active;
        if (active > limit) {
            pruned += this->data->prune (limit / 2);
            if ((active - pruned) > limit) {
                pruned += this->threads->prune (limit / 6);
                pruned += this->channels->prune (limit / 6);
                pruned += this->identities->prune (limit / 6);
            }
        }
    }

    if (optimized || pruned) {
        this->report (log::level::note, 8,
                      optimized, this->settings.shard_trimming_threshold,
                      pruned, limit);
    }
}
