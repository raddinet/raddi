#include "raddi_iid.h"
#include <cstdio>

std::size_t raddi::iid::serialize (wchar_t * buffer, std::size_t size) const {
    auto n = std::swprintf (buffer, size, L"%08x%08x", this->nonce, this->timestamp);
    if ((n >= iid::min_length) && (n <= iid::max_length))
        return n;
    else
        return 0;
}

std::size_t raddi::iid::parse (const wchar_t * string) {
    std::size_t offset;
    if (std::swscanf (string, L" %8x%x%zn", &this->nonce, &this->timestamp, &offset) == 2)
        return offset;
    else
        return 0;
}

std::size_t raddi::iid::parse (const char * string) {
    std::size_t offset;
    if (std::sscanf (string, " %8x%x%zn", &this->nonce, &this->timestamp, &offset) == 2)
        return offset;
    else
        return 0;
}
