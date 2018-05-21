#include"raddi_channel.h"
#include"raddi_timestamp.h"

bool raddi::channel::create (const raddi::iid & identity) {
    this->id.timestamp = raddi::now ();
    this->id.identity = identity;
    this->parent = this->id;
    return true;
}
