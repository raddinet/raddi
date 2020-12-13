#include"raddi_channel.h"
#include"raddi_timestamp.h"

bool raddi::channel::create (const raddi::iid & identity, std::uint32_t timestamp) {
    this->id.timestamp = timestamp;
    this->id.identity = identity;
    this->parent = this->id;
    return true;
}
