#ifndef RADDI_MONITOR_H
#define RADDI_MONITOR_H

#include "../common/log.h"
#include "../node/server.h"
#include <vector>

// Monitor
//  - 
//  - log codes used:
//     - notes: 1, 2, 3
//     - events: 2, 3
//     - errors: 5, 6, 7
//
template <raddi::component LogProviderComponent>
class Monitor
    : public Overlapped
    , private virtual raddi::log::provider <LogProviderComponent> {

    HANDLE directory;

    bool active = false;
    bool retry = false;

    std::vector <unsigned char> buffer;
    
    bool next ();
    void completion (bool success, std::size_t n) override; // Overlapped

protected:

    // render_directory_path
    //  - requests user to occassionaly (rarely) re-generate path of 'directory'
    //  - trailing backslash is required
    //
    virtual std::wstring render_directory_path () const = 0;

    // process
    //  - passes name of a file that has appeared/changed to user for processing
    //
    virtual bool process (const std::wstring & filename) = 0;

public:
    Monitor (HANDLE);
    Monitor (const std::wstring &);
    ~Monitor ();

    bool start ();
    bool stop ();
};

#include "monitor.tcc"
#endif

