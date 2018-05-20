#ifndef RADDI_SOURCE_H
#define RADDI_SOURCE_H

#include <windows.h>
#include <vector>

#include "server.h"

#include "../common/log.h"
#include "../common/monitor.h"
#include "../core/raddi_entry.h"
#include "../core/raddi_command.h"

struct SourceState
    : private virtual raddi::log::provider <raddi::component::source> {

    std::wstring  path;
    HANDLE        handle = INVALID_HANDLE_VALUE;
    bool          created = false;

    SourceState (const wchar_t * path, const wchar_t * nonce);
};

class Source
    : SourceState
    , Monitor <raddi::component::source>
    , virtual raddi::log::provider <raddi::component::source> {

    bool entry (const raddi::entry * entry, std::size_t size);
    bool command (const raddi::command *, std::size_t size);

    virtual bool process (const std::wstring & filename) override;
    virtual std::wstring render_directory_path () const override { return this->path; }

public:
    Source (const wchar_t * path, const wchar_t * nonce);
    ~Source ();

    using SourceState::path;
    using Monitor <raddi::component::source> ::start;
    using Monitor <raddi::component::source> ::stop;

    std::size_t total = 0;
};

#endif

