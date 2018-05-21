#include "raddi_eid.h"

#include <cstdio>
#include <cwchar>

bool raddi::eid::serialize (wchar_t * buffer, std::size_t size) const {
    if (auto offset = this->identity.serialize (buffer, size)) {
        return std::swprintf (buffer + offset, size - offset, L"-%x", this->timestamp) > 1;
    } else
        return false;
}

std::size_t raddi::eid::parse (const wchar_t * string) {
    if (std::size_t offset = this->identity.parse (string)) {
        std::size_t offset2;
        if (std::swscanf (string + offset, L" - %8x%zn", &this->timestamp, &offset2) == 1) {
            return offset + offset2;
        }
    }
    return false;
}
