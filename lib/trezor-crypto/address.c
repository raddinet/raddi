/**
 * Copyright (c) 2016 Daira Hopwood
 * Copyright (c) 2016 Pavol Rusnak
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * NOTE:
 * This is modified and highly pruned version of the original source code.
 * Get original: https://github.com/trezor/trezor-firmware/tree/master/crypto
 */

#include "address.h"
#include "bignum.h"

size_t ecdsa_address_prefix_bytes_len(uint32_t address_type) {
  if (address_type <= 0xFF) return 1;
  if (address_type <= 0xFFFF) return 2;
  if (address_type <= 0xFFFFFF) return 3;
  return 4;
}

size_t ecdsa_address_write_prefix_bytes(uint32_t address_type, uint8_t *out) {
  uint8_t * out_ = out;

  if (address_type > 0xFFFFFF) *(out++) = address_type >> 24;
  if (address_type > 0xFFFF) *(out++) = (address_type >> 16) & 0xFF;
  if (address_type > 0xFF) *(out++) = (address_type >> 8) & 0xFF;
  *(out++) = address_type & 0xFF;

  return out - out_;
}

bool ecdsa_address_check_prefix(const uint8_t *addr, uint32_t address_type) {
  if (address_type <= 0xFF) {
    return address_type == (uint32_t)(addr[0]);
  }
  if (address_type <= 0xFFFF) {
    return address_type == (((uint32_t)addr[0] << 8) | ((uint32_t)addr[1]));
  }
  if (address_type <= 0xFFFFFF) {
    return address_type == (((uint32_t)addr[0] << 16) |
                            ((uint32_t)addr[1] << 8) | ((uint32_t)addr[2]));
  }
  return address_type ==
         (((uint32_t)addr[0] << 24) | ((uint32_t)addr[1] << 16) |
          ((uint32_t)addr[2] << 8) | ((uint32_t)addr[3]));
}
