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

#pragma warning (disable:26812) // unscoped enum warning

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

    if (directory::create_full_path (path.c_str ())) {
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

            file f;
            if (f.open (path + L"\\xor", file::mode::always, mode, file::share::read, file::buffer::sequential)) {
                if (f.created ()
                    && mode == file::access::write
                    && this->identities->empty ()
                    && this->channels->empty ()
                    && this->threads->empty ()
                    && this->data->empty ()) {

                    auto n = 256;
                    this->mask.resize (n);
                    randombytes_buf (&this->mask [0], n);
                    if (!f.write (&this->mask [0], n)) {
                        this->mask.clear ();
                    }
                } else {
                    if (auto n = (std::size_t) f.size ()) {
                        this->mask.resize (n);
                        if (!f.read (&this->mask [0], n)) {
                            this->mask.clear ();
                        }
                    }
                }
                f.close ();
            }

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

raddi::db::assessment raddi::db::assess (const void * data, std::size_t size, _Out_ root * top, _Out_ assessed_level * level) {
    const auto entry = static_cast <const raddi::entry *> (data);
    const auto type = entry->is_announcement ();

    // find identity, validate signature, on failure add negative mark to the connection

    switch (type) {
        case raddi::entry::new_identity_announcement:
            if (!static_cast <const raddi::identity *> (entry)->verify (size)) {

                this->report (log::level::data, 2, entry->id.serialize ());
                return raddi::db::rejected;
            }
            break;

        case raddi::entry::new_channel_announcement:
        case raddi::entry::not_an_announcement:

            // find announcement of author of this entry
            //  - note that 'author' identity instance allocates only stack space for fixed fields (public_key)

            raddi::identity author;
            if (!this->identities->get (entry->id.identity, read::content,
                                        &author, nullptr, sizeof (identity::public_key))) {

                this->report (log::level::data, 3, entry->id.serialize ());
                return raddi::db::rejected;
            }

            // verify it's signed by that author

            if (!entry->verify (size, author.public_key)) {

                this->report (log::level::data, 4, entry->id.serialize ());
                return raddi::db::rejected;
            }
            break;
    }

    // verify that 'identity' and 'channel' announcement is plain line of text
    //  - that means no control data (like upvote or attachment), international characters (full unicode) are still allowed

    switch (type) {
        case raddi::entry::new_identity_announcement:
            if (!raddi::content::is_plain_line (entry->content () + raddi::identity::overhead_size,
                                                size - sizeof (raddi::entry) - raddi::identity::overhead_size)) {

                this->report (log::level::data, 5, entry->id.serialize ());
                return raddi::db::rejected;
            }
            break;

        case raddi::entry::new_channel_announcement:
            if (!raddi::content::is_plain_line (entry->content (),
                                               size - sizeof (raddi::entry))) {

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
            *level = assessed_level::top;
            return raddi::db::required;

        case raddi::entry::not_an_announcement:
            if (this->channels->get (entry->parent)
                    || ((entry->parent.timestamp == entry->parent.identity.timestamp) && this->identities->get (entry->parent.identity))) {

                // parent is normal channel or identity channel: entry is a thread, either in a normal channel or in an identity channel
                top->thread = entry->id;
                top->channel = entry->parent;
                *level = assessed_level::thread;
                return raddi::db::classify;

            } else {
                raddi::db::trow tr;
                if (this->threads->get (entry->parent, &tr)) {

                    // parent is thread: entry is a top level comment within a thread (or vote on the thread)
                    top->channel = tr.parent;
                    top->thread = entry->parent;
                    *level = assessed_level::reply;
                    return raddi::db::classify;

                } else {
                    raddi::db::row r;
                    if (this->data->get (entry->parent, &r)) {

                        // parent is normal entry: nested comment, vote or stuff
                        *top = r.top ();
                        *level = assessed_level::nested;
                        return raddi::db::classify;
                    } else
                        return raddi::db::detached;
                }
            }
    }
    return raddi::db::rejected; // unreachable
}

bool raddi::db::insert (const entry * entry, std::size_t size, const root & top, bool & exists) {

    // when inserting to table, size is always less or equal to (0xFFFF - 16) (AES overhead)
    // and gets reduced by 88 bytes (entry header) thus allowing us to add up to 104 bytes
    // of additional data should that be neccessary

    switch (entry->is_announcement ()) {
        case raddi::entry::new_identity_announcement:
            return this->identities->insert (entry, size, top, exists);
        case raddi::entry::new_channel_announcement:
            return this->channels->insert (entry, size, top, exists);

        case raddi::entry::not_an_announcement:
            if ((entry->id == top.thread) && (entry->parent == top.channel)) {
                return this->threads->insert (entry, size, top, exists)
                    && this->data->insert (entry, size, top, exists);
            }
    }
    return this->data->insert (entry, size, top, exists);
}

bool raddi::db::erase (const eid & entry, bool thorough) {
    if (entry.identity.timestamp != entry.timestamp) {
        this->threads->erase (entry, thorough);
        return this->data->erase (entry, thorough)
            || this->channels->erase (entry, thorough);
    } else
        return this->identities->erase (entry.identity, thorough);
}

bool raddi::db::get (const eid & entry, void * buffer, std::size_t * length) const {
    if (entry.identity.timestamp != entry.timestamp)
        return this->data->get (entry, buffer, length)
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
