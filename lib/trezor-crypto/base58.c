/**
 * Copyright (c) 2012-2014 Luke Dashjr
 * Copyright (c) 2013-2014 Pavol Rusnak
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

#include "base58.h"
#include <stdbool.h>
#include <string.h>

static const int8_t b58digits_map[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,
    8,  -1, -1, -1, -1, -1, -1, -1, 9,  10, 11, 12, 13, 14, 15, 16, -1, 17, 18,
    19, 20, 21, -1, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1,
    -1, -1, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, -1, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 57, -1, -1, -1, -1, -1,
};

typedef uint64_t b58_maxint_t;
typedef uint32_t b58_almostmaxint_t;
#define b58_almostmaxint_bits (sizeof(b58_almostmaxint_t) * 8)
static const b58_almostmaxint_t b58_almostmaxint_mask =
    ((((b58_maxint_t)1) << b58_almostmaxint_bits) - 1);

// Decodes a null-terminated Base58 string `b58` to binary and writes the result
// at the end of the buffer `bin` of size `*binszp`. On success `*binszp` is set
// to the number of valid bytes at the end of the buffer.
static bool b58tobin(void *bin, size_t *binszp, const char *b58) {
  size_t binsz = *binszp;

  if (binsz == 0) {
    return false;
  }
  if (binsz > 256) {
      return false;
  }

  const unsigned char *b58u = (const unsigned char *)b58;
  unsigned char *binu = bin;
  size_t outisz =
      (binsz + sizeof(b58_almostmaxint_t) - 1) / sizeof(b58_almostmaxint_t);
  b58_almostmaxint_t outi[64/*outisz*/];
  b58_maxint_t t = 0;
  b58_almostmaxint_t c = 0;
  size_t i = 0, j = 0;
  uint8_t bytesleft = binsz % sizeof(b58_almostmaxint_t);
  b58_almostmaxint_t zeromask =
      bytesleft ? (b58_almostmaxint_mask << (bytesleft * 8)) : 0;
  unsigned zerocount = 0;

  size_t b58sz = strlen(b58);

  memzero(outi, sizeof(outi));

  // Leading zeros, just count
  for (i = 0; i < b58sz && b58u[i] == '1'; ++i) ++zerocount;

  for (; i < b58sz; ++i) {
    if (b58u[i] & 0x80)
      // High-bit set on invalid digit
      return false;
    if (b58digits_map[b58u[i]] == -1)
      // Invalid base58 digit
      return false;
    c = (unsigned)b58digits_map[b58u[i]];
    for (j = outisz; j--;) {
      t = ((b58_maxint_t)outi[j]) * 58 + c;
      c = t >> b58_almostmaxint_bits;
      outi[j] = t & b58_almostmaxint_mask;
    }
    if (c)
      // Output number too big (carry to the next int32)
      return false;
    if (outi[0] & zeromask)
      // Output number too big (last int32 filled too far)
      return false;
  }

  j = 0;
  if (bytesleft) {
    for (i = bytesleft; i > 0; --i) {
      *(binu++) = (outi[0] >> (8 * (i - 1))) & 0xff;
    }
    ++j;
  }

  for (; j < outisz; ++j) {
    for (i = sizeof(*outi); i > 0; --i) {
      *(binu++) = (outi[j] >> (8 * (i - 1))) & 0xff;
    }
  }

  // locate the most significant byte
  binu = bin;
  for (i = 0; i < binsz; ++i) {
    if (binu[i]) break;
  }

  // prepend the correct number of null-bytes
  if (zerocount > i) {
    /* result too large */
    return false;
  }
  *binszp = binsz - i + zerocount;

  return true;
}

int base58_decoded_check (const void *bin, size_t binsz, uint32_t checksum, const uint8_t hash [32], const char *base58str) {
  const uint8_t *binc = bin;
  unsigned i = 0;
  if (binsz < 4) return -4;
  if (memcmp(&checksum, hash, 4)) return -1;

  // Check number of zeros is correct AFTER verifying checksum (to avoid
  // possibility of accessing base58str beyond the end)
  for (i = 0; binc[i] == '\0' && base58str[i] == '1'; ++i) {
  }  // Just finding the end of zeros, nothing to do in loop
  if (binc[i] == '\0' || base58str[i] == '1') return -3;

  return binc[0];
}

size_t base58_decode (const char * str, uint8_t * data, size_t datalen, uint32_t * checksum) {
  if (datalen > 128) {
    return 0;
  }
  uint8_t d [128 + 4];
  memset (d, 0, sizeof (d));
  size_t res = datalen + 4;
  if (b58tobin (d, &res, str) != true) {
    return 0;
  }
  uint8_t * nd = d + datalen + 4 - res;
  memcpy (data, nd, res - 4);
  if (checksum) {
    memcpy (checksum, nd + res - 4, 4);
  }
  return res - 4;
}
