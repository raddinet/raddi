#ifndef DATA_H
#define DATA_H

#include <windows.h>
#include "../lib/SQLite.hpp"
#include "../common/file.h"

class Data : public SQLite {
    file lock;
public:
    struct {
        Statement insert; // id
        Statement update; // x, y, w, h, m, WHERE id
        Statement close [5]; // WHERE id
        Statement maxID; // SELECT MAX(id)
    } windows;

    struct {
        Statement query; // id, stack, entry, scroll, t, WHERE window
        Statement update; // window, id, stack, entry, scroll
        Statement close [2]; // [0] = window, id, title, [1] = window, id
        // Statement maxID; // SELECT id WHERE window
        // Statement clear; // WHERE window
    } tabs;

    struct {
        Statement query; // id, name
        Statement insert; // id, name
        Statement rename; // UPDATE text WHERE id
        Statement remove [3]; // id

        struct {
            Statement insert; // id, list, name
            Statement query; // SELECT id, list, name
            Statement move; // UPDATE group WHERE id
            Statement rename; // UPDATE text WHERE id
            Statement remove [2]; // id
            Statement cleanup; // delete empty groups from `list`
            Statement maxID; // SELECT MAX(id)
        } groups;
        struct {
            Statement insert; // group, entry
            Statement query; // SELECT id, group, eid
            Statement get; // SELECT eid WHERE id
            Statement move; // SET group WHERE id
            Statement remove; // group, id
            Statement maxID; // SELECT MAX(id)
        } data;
    } lists;

    struct {
        Statement load; // SELECT value WHERE window, id
        Statement save; // window, id, value
    } property;

    struct {
        Statement list; // SELECT eid, title
        Statement last [2]; // SELECT eid, title, i; DELETE...
        Statement prune; // n (number of entries to keep)
        Statement clear;
    } history;

    struct {
        Statement get;
        Statement set;
    } current;

    struct {
        Statement is;
        Statement set;
        Statement get;
        Statement remove;

        Statement setByListedId;
        Statement getByListedId;
        Statement removeByListedId;
    } names;

    struct {
        Statement size;
        Statement list;
    } identities;

public:
    bool initialize (HINSTANCE);
    void close ();

private:
    bool locate_and_open (); // also forwards requests to already running instance
    bool open_data_file (const wchar_t * filename, bool create_separated_if_new = false);
    bool resolve_separation (const wchar_t * filename, bool create_separated_if_new);
    bool forward_requests (unsigned int * n = nullptr);
    bool prepare_queries ();
};

#endif
