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
        Statement maxID; // SELECT MAX(id)

        struct {
            Statement query; // SELECT id, name WHERE list
        } subs;
        struct {
            Statement query; // SELECT sub, eid, name WHERE list
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
