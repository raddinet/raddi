#include "counter.h"
#include <windows.h>

void counter::operator += (std::size_t value) noexcept {
    InterlockedAdd64 ((volatile LONG64 *) &this->bytes, value);
    InterlockedIncrement (&this->n);
}
