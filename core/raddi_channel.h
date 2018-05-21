#ifndef RADDI_CHANNEL_H
#define RADDI_CHANNEL_H

#include "raddi_entry.h"
#include <sodium.h>
#include <cstring>

namespace raddi {

    // channel
    //  - channel announcement header
    //  - while IDENTITY basically is a channel, its announcement contents are further specialized
    // 
    struct channel : public entry {

        // create
        //  - creates new announcement entry id
        //
        bool create (const raddi::iid & identity);

        // max_description_size
        //  - maximum length of identity/channel description content following announcement header
        //  - basically max content size minus space occupied by above introduced fields
        // 
        static constexpr std::size_t max_description_size = raddi::entry::max_content_size;
    };
}

#endif
