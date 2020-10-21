#include "data.h"
#include "../common/errorbox.h"
#include "../common/options.h"
#include "../common/directory.h"
#include "../core/raddi_defaults.h"

#include <algorithm>
#include <shlobj.h>

bool Data::initialize (HINSTANCE hInstance) {
    if (SQLite::Initialize ()) {
        if (this->locate_and_open () && this->prepare_queries ()) {
            return true;
        }
    } else {
        StopBox (0x03);
    }
    return false;
}

void Data::close () {
    this->SQLite::close ();
    this->lock.close ();
}

bool Data::locate_and_open () {
    if (auto parameter = option (__argc, __wargv, L"data"))
        return this->open_data_file (parameter);

    // raddi.exe -> raddi.db
    //  - simply replace extension to enable user to store and run two different versions alongside

    wchar_t exepath [32767];
    if (GetModuleFileName (NULL, exepath, sizeof exepath / sizeof exepath [0])) {

        auto file = std::wcsrchr (exepath, L'\\');
        if (file != nullptr) {
            file++;
        } else {
            file = exepath;
        }

        if (auto extension = std::wcsrchr (file, L'.')) {
            extension [1] = L'd';
            extension [2] = L'b';
            extension [3] = L'\0';
        } else {
            std::wcscat (file, L".db");
        }

        // current directory

        if (GetFileAttributes (file) != INVALID_FILE_ATTRIBUTES) {
            return this->open_data_file (file);
        } else {
            if (GetLastError () != ERROR_FILE_NOT_FOUND) {
                StopBox (0x04, file);
                return false;
            }
        }

        // executable directory

        if (GetFileAttributes (exepath) != INVALID_FILE_ATTRIBUTES) {
            return this->open_data_file (exepath);
        } else {
            if (GetLastError () != ERROR_FILE_NOT_FOUND) {
                StopBox (0x04, file);
                return false;
            }
        }

        // finally try standard user local app data
        //  - whilst this is fourth attempt, this path is expected to be the most common

        wchar_t path [2 * MAX_PATH];
        if (SHGetFolderPath (NULL, CSIDL_APPDATA, NULL, 0, path) == S_OK) {

            std::wcscat (path, raddi::defaults::data_subdir);
            std::wcscat (path, raddi::defaults::app_subdir);
            directory::create_full_path (path);

            std::wcscat (path, file);
            return this->open_data_file (path, true);
        } else
            return this->open_data_file (exepath);
    } else {
        StopBox (0x01);
        return false;
    }

}

bool Data::open_data_file (const wchar_t * filename, bool create_separated_if_new) {
    if (this->open (filename)) {

        auto lockname = std::wstring (filename) + L".lock";
        if (this->lock.open (lockname, file::mode::create, file::access::write, file::share::read)) {
            this->lock.write (GetCurrentThreadId ());
            this->lock.write (GetCurrentProcessId ());
            this->lock.write (GetCurrentProcessSessionId ());

            return this->resolve_separation (filename, create_separated_if_new)
                && this->forward_requests ()
                && this->prepare_queries ();
        } else
        if (this->lock.open (lockname, file::mode::open, file::access::read, file::share::full)) {

            DWORD tid, pid, sid;
            if (this->lock.read (tid) &&
                this->lock.read (pid) &&
                this->lock.read (sid)) {

                if (sid == GetCurrentProcessSessionId ()) {
                    auto n = 0u;
                    if (this->resolve_separation (filename, false) && this->forward_requests (&n)) {
                        AllowSetForegroundWindow (pid);
                        if (!PostThreadMessage (tid, WM_APP, 0, n)) {
                            StopBox (0x0A, filename);
                        }
                    }
                    return false;
                } else {
                    StopBox (0x08, filename);
                    return false;
                }
            }
        }
    }

    StopBox (0x07, filename);
    return false;
}

bool Data::resolve_separation (const wchar_t * filename, bool create_separated_if_new) {
    raddi::log::event (0x03, filename);

    if (IsPathAbsolute (filename)) {
        SetCurrentDirectory (std::wstring (filename, std::wcsrchr (filename, L'\\')).c_str ());
    }

    try {
        std::wstring keys = L"`main`";
        std::wstring state = L"`main`";
        bool keys_separate = false;
        bool state_separate = false;

        auto attach = this->prepare (L"ATTACH ? AS ?"); // SQLITE_MAX_ATTACHED == 10
        bool created = !this->query <long long> (L"SELECT COUNT(*) FROM `sqlite_master`");

        if (created) {
            this->execute (L"CREATE TABLE `separation`(`name` TEXT NOT NULL, `path` TEXT NOT NULL)");

            if (create_separated_if_new) {
                keys = L"`keys`";
                state = L"`state`";
                keys_separate = true;
                state_separate = true;

                auto insert = this->prepare (L"INSERT INTO `separation`(`name`,`path`) VALUES (?,?)");
                insert (L"state", L"state.db"); // temporary app state
                insert (L"keys", L"keys.db"); // private keys
                attach (L"state.db", L"state");
                attach (L"keys.db", L"keys");
            }
        } else {
            try {
                auto select = this->prepare (L"SELECT `name`,`path` FROM `separation`");
                while (select.next ()) {
                    auto name = select.get <std::wstring> (0);
                    auto path = select.get <std::wstring> (1);

                    std::replace (path.begin (), path.end (), L'/', L'\\');
                    attach (path, name);

                    if (name == L"keys") { keys = L"`keys`"; keys_separate = true; }
                    if (name == L"state") { state = L"`state`";  state_separate = true; }
                }
            } catch (const SQLite::Exception & x) {
                ErrorBox (0x01, x.what ());
            }
        }

        // TODO: consider extracting to script resource (from .txt) and just replace wildcards for 'keys' and 'state'

        // this->execute (L"PRAGMA main.cache_size = 32");
        // this->execute (L"PRAGMA keys.cache_size = 64");
        // this->execute (L"PRAGMA state.cache_size = 2048");

        // this->execute (L"PRAGMA secure_delete = on");
        
        if (state_separate) {
            // state is not critical and frequently updated, TODO: option?
            // this->execute (L"PRAGMA state.synchronous = off");
            // this->execute (L"PRAGMA state.journal_mode = memory");
        }

        // make sure all tables are there
        this->execute (L"CREATE TABLE IF NOT EXISTS " + keys + L".`pool`       (`key` BLOB PRIMARY KEY) WITHOUT ROWID");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + keys + L".`identities` (`iid` BLOB PRIMARY KEY, `key` BLOB) WITHOUT ROWID");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + keys + L".`decryption` (`key` BLOB PRIMARY KEY) WITHOUT ROWID");
        
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`requests`  (`entry` TEXT)");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`windows`   (`id` INTEGER PRIMARY KEY, `x` INT, `y` INT, `w` INT, `h` INT, `m` INT)");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`properties`(`window` INT, `id` INT, `value` INT, PRIMARY KEY (`window`,`id`)) WITHOUT ROWID");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`tabs`      (`window` INT, `id` INT, `stack` INT, `entry` BLOB, `scroll` INT, `t` INT, PRIMARY KEY (`window`,`id`)) WITHOUT ROWID");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`collapsed` (`window` INT, `tab` INT, `entry` BLOB, PRIMARY KEY (`window`,`tab`)) WITHOUT ROWID");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`concepts`  (`window` INT, `tab` INT, `entry` BLOB, `text` TEXT, PRIMARY KEY (`window`,`tab`,`entry`)) WITHOUT ROWID");
        
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`history`   (`entry` BLOB, `t` INT, `title` TEXT, PRIMARY KEY (`entry`)) WITHOUT ROWID");
        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`current`   (`window` INT, `name` TEXT, `value` INT, PRIMARY KEY (`window`,`name`)) WITHOUT ROWID");

        this->execute (L"CREATE TABLE IF NOT EXISTS " + state + L".`resolved`  (`entry` BLOB, `title` TEXT, PRIMARY KEY (`entry`)) WITHOUT ROWID");


        // this->execute (L"CREATE TABLE IF NOT EXISTS `pinned` (`entry` BLOB PRIMARY KEY) WITHOUT ROWID"); // channels/identities??
        this->execute (L"CREATE TABLE IF NOT EXISTS `names` (`entry` BLOB PRIMARY KEY, `name` TEXT) WITHOUT ROWID"); // custom names for channels/identities

        this->execute (L"CREATE TABLE IF NOT EXISTS `lists` (`id` INTEGER PRIMARY KEY, `name` TEXT)");
        this->execute (L"CREATE TABLE IF NOT EXISTS `groups` (`id` INTEGER PRIMARY KEY, `list` INT, `name` TEXT)");
        this->execute (L"CREATE TABLE IF NOT EXISTS `listed` (`id` INTEGER PRIMARY KEY, `group` INT, `entry` BLOB, `name` TEXT)");

        // table to remember behavior (checkboxes in dialogs)
        this->execute (L"CREATE TABLE IF NOT EXISTS `remember` (`dialog` TEXT PRIMARY KEY, `action` INT) WITHOUT ROWID");

        // groups (sets?) on the left side
        // subscriptions
        // custom nicknames for iid/eid subscribed channels and users (friends)
        // subscribed moderators for channels, level of influence, modops that apply (weight?) ...author influence?
        // channel sorting, vote type weights, merging, etc.
        // locally banned (hidden) identities, per thread/channel/everything
        // pending entries !!!
        // sent messages
        // notes?
        // main.settings
        //  - delay, min diff level, 

        this->execute (L"CREATE TABLE IF NOT EXISTS `app`(`property` TEXT PRIMARY KEY, `value` TEXT NOT NULL) WITHOUT ROWID");
        this->execute (L"CREATE TABLE IF NOT EXISTS `settings`(`name` TEXT PRIMARY KEY, `value` TEXT NOT NULL) WITHOUT ROWID");

        if (created) {
            // TODO: generate predefined custom group/set
            // TODO: generate some open tabs with hello/help

        }

        return true;
    } catch (const SQLite::Exception & x) {
        StopBox (5, x.what ());
        return false;
    }
}

bool Data::forward_requests (unsigned int * n) {
    if (n) {
        *n = 0;
    }
    try {
        auto insert = this->prepare (L"INSERT INTO `requests` VALUES (?)");
        options (__argc, __wargv, L"raddi", [&insert, n] (const wchar_t * uri) {
            while (*uri == L'/') {
                ++uri;
            }
            if (*uri) {
                insert.execute (uri);
                if (n) {
                    ++*n;
                }
            }
        });
        return true;
    } catch (const SQLite::Exception & x) {
        StopBox (6, x.what ());
        return false;
    }
}

bool Data::prepare_queries () {
    try {
        SetLastError (ERROR_INTERNAL_ERROR);

        this->windows.maxID = this->prepare (L"SELECT MAX(`id`) FROM `windows`");
        this->windows.insert = this->prepare (L"INSERT INTO `windows` (`id`,`x`,`y`,`w`,`h`,`m`) VALUES (?,0,0,0,0,0)");
        this->windows.update = this->prepare (L"UPDATE OR IGNORE `windows` SET `x`=?,`y`=?,`w`=?,`h`=?,`m`=? WHERE `id`=?");
        this->windows.close [0] = this->prepare (L"DELETE FROM `windows` WHERE `id`=?");
        this->windows.close [1] = this->prepare (L"DELETE FROM `tabs` WHERE `window`=?");
        this->windows.close [2] = this->prepare (L"DELETE FROM `properties` WHERE `window`=?");
        this->windows.close [3] = this->prepare (L"DELETE FROM `concepts` WHERE `window`=?");
        this->windows.close [4] = this->prepare (L"DELETE FROM `collapsed` WHERE `window`=?");

        this->tabs.query = this->prepare (L"SELECT `id`,`stack`,`entry`,`scroll`,`t` FROM `tabs` WHERE `window`=? ORDER BY `stack` ASC");
        this->tabs.update = this->prepare (L"REPLACE INTO `tabs`(`window`,`id`,`stack`,`entry`,`scroll`,`t`) VALUES (?,?,?,?,?,strftime('%s','now'))");
        this->tabs.close [0] = this->prepare (L"REPLACE INTO `history` (`t`,`entry`,`title`) VALUES (strftime('%s','now'), (SELECT `entry` FROM `tabs` WHERE `window`=? AND `id`=?),?)");
        this->tabs.close [1] = this->prepare (L"DELETE FROM `tabs` WHERE `window`=? AND `id`=?");
        // this->tabs.maxID = this->prepare (L"SELECT MAX(`id`) FROM `tabs` WHERE `window`=?");
        // this->tabs.clear = this->prepare (L"DELETE FROM `tabs` WHERE `window`=?");

        this->lists.query = this->prepare (L"SELECT `id`,`name` FROM `lists`");
        this->lists.insert = this->prepare (L"REPLACE INTO `lists`(`id`,`name`) VALUES (?,?)");
        this->lists.rename = this->prepare (L"UPDATE `lists` SET `name`=? WHERE `id`=?");
        this->lists.remove [0] = this->prepare (L"DELETE FROM `listed` WHERE `group` IN (SELECT `id` FROM `groups` WHERE `list`=?)");
        this->lists.remove [1] = this->prepare (L"DELETE FROM `groups` WHERE `list`=?");
        this->lists.remove [2] = this->prepare (L"DELETE FROM `lists` WHERE `id`=?");
        
        this->lists.groups.insert = this->prepare (L"INSERT INTO `groups` (`id`,`list`,`name`) VALUES (?,?,?)");
        this->lists.groups.query = this->prepare (L"SELECT `id`,`list`,`name` FROM `groups` ORDER BY `id` ASC");
        this->lists.groups.move = this->prepare (L"UPDATE `groups` SET `list`=? WHERE `id`=?");
        this->lists.groups.rename = this->prepare (L"UPDATE `groups` SET `name`=? WHERE `id`=?");
        this->lists.groups.remove [0] = this->prepare (L"DELETE FROM `groups` WHERE `id`=?");
        this->lists.groups.remove [1] = this->prepare (L"DELETE FROM `listed` WHERE `group`=?");
        this->lists.groups.cleanup = this->prepare (L"DELETE FROM `groups` WHERE `list`=? AND `id` NOT IN (SELECT DISTINCT `group` FROM `listed`)");
        this->lists.groups.maxID = this->prepare (L"SELECT COALESCE(MAX(`id`),0) FROM `groups`");

        this->lists.data.insert = this->prepare (L"INSERT INTO `listed` (`id`,`group`,`entry`) VALUES ((SELECT MAX(`id`)+1 FROM `listed`),?,?)");
        this->lists.data.query = this->prepare (L"SELECT `id`,`group`,`entry` FROM `listed`");
        this->lists.data.get = this->prepare (L"SELECT `entry` FROM `listed` WHERE `id`=?");
        this->lists.data.move = this->prepare (L"UPDATE `listed` SET `group`=? WHERE `id`=?");
        this->lists.data.maxID = this->prepare (L"SELECT COALESCE(MAX(`id`),0) FROM `listed`");
        this->lists.data.remove = this->prepare (L"DELETE FROM `listed` WHERE `id`=?");

        this->names.is = this->prepare (L"SELECT COUNT(*) FROM `names` WHERE `entry`=?");
        this->names.get = this->prepare (L"SELECT `name` FROM `names` WHERE `entry`=?");
        this->names.set = this->prepare (L"REPLACE INTO `names` (`entry`,`name`) VALUES (?,?)");
        this->names.remove = this->prepare (L"DELETE FROM `names` WHERE `entry`=?");
        this->names.getByListedId = this->prepare (L"SELECT `name` FROM `names` WHERE `entry`=(SELECT `entry` FROM `listed` WHERE `id`=?)");
        this->names.setByListedId = this->prepare (L"REPLACE INTO `names` (`entry`,`name`) VALUES ((SELECT `entry` FROM `listed` WHERE `id`=?),?)");
        this->names.removeByListedId = this->prepare (L"DELETE FROM `names` WHERE `entry`=(SELECT `entry` FROM `listed` WHERE `id`=?)");

        this->history.list = this->prepare (L"SELECT `entry`,`title` FROM `history` ORDER BY `t` DESC");
        this->history.last [0] = this->prepare (L"SELECT `entry`,`title`,`t` FROM `history` ORDER BY `t` DESC LIMIT 1");
        this->history.last [1] = this->prepare (L"DELETE FROM `history` WHERE `entry` = (SELECT `entry` FROM `history` ORDER BY `t` DESC LIMIT 1)");
        this->history.prune = this->prepare (L"DELETE FROM `history` WHERE `t` < (SELECT `t` FROM `history` ORDER BY `t` DESC LIMIT 1 OFFSET ?)");
        this->history.clear = this->prepare (L"DELETE FROM `history`");

        this->property.save = this->prepare (L"REPLACE INTO `properties` (`window`,`id`,`value`) VALUES (?,?,?)");
        this->property.load = this->prepare (L"SELECT `value` FROM `properties` WHERE `window`=? AND `id`=?");

        this->current.set = this->prepare (L"REPLACE INTO `current` (`window`,`name`,`value`) VALUES (?,?,?)");
        this->current.get = this->prepare (L"SELECT `value` FROM `current` WHERE `window`=? AND `name`=?");

        this->identities.size = this->prepare (L"SELECT COUNT(*) FROM `identities`");

        SetLastError (ERROR_SUCCESS);
        return true;
    } catch (const SQLite::PrepareException & x) {
        StopBox (0x0B, x.what (), x.query);
        return false;
    }
}
