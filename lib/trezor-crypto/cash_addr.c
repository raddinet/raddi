/* Copyright (c) 2017 Jochen Hoenicke
 * based on code Copyright (c) 2017 Peter Wuille
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * NOTE:
 * This is modified and highly pruned version of the original source code.
 * Get original: https://github.com/trezor/trezor-firmware/tree/master/crypto
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cash_addr.h"

#define MAX_CASHADDR_SIZE 129
#define MAX_BASE32_SIZE 104
#define MAX_DATA_SIZE 65
#define MAX_HRP_SIZE 20
#define CHECKSUM_SIZE 8

static uint64_t cashaddr_polymod_step(uint64_t pre) {
  uint8_t b = pre >> 35;
  return ((pre & 0x7FFFFFFFFULL) << 5) ^ (-((b >> 0) & 1) & 0x98f2bc8e61ULL) ^
         (-((b >> 1) & 1) & 0x79b76d99e2ULL) ^
         (-((b >> 2) & 1) & 0xf33e5fb3c4ULL) ^
         (-((b >> 3) & 1) & 0xae2eabe2a8ULL) ^
         (-((b >> 4) & 1) & 0x1e4f43e470ULL);
}

static const int8_t charset_rev[128] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 15, -1, 10, 17, 21, 20, 26, 30, 7,
    5,  -1, -1, -1, -1, -1, -1, -1, 29, -1, 24, 13, 25, 9,  8,  23, -1, 18, 22,
    31, 27, 19, -1, 1,  0,  3,  16, 11, 28, 12, 14, 6,  4,  2,  -1, -1, -1, -1,
    -1, -1, 29, -1, 24, 13, 25, 9,  8,  23, -1, 18, 22, 31, 27, 19, -1, 1,  0,
    3,  16, 11, 28, 12, 14, 6,  4,  2,  -1, -1, -1, -1, -1};

static int cash_decode(char* hrp, uint8_t* data, size_t* data_len, const char* input) {
  uint64_t chk = 1;
  size_t i = 0;
  size_t input_len = strlen(input);
  size_t hrp_len = 0;
  int have_lower = 0, have_upper = 0;
  if (input_len < CHECKSUM_SIZE || input_len > MAX_CASHADDR_SIZE) {
    return 0;
  }
  *data_len = 0;
  while (*data_len < input_len && input[(input_len - 1) - *data_len] != ':') {
    ++(*data_len);
  }
  hrp_len = input_len - (1 + *data_len);
  if (1 + *data_len >= input_len || hrp_len > MAX_HRP_SIZE ||
      *data_len < CHECKSUM_SIZE ||
      *data_len > CHECKSUM_SIZE + MAX_BASE32_SIZE) {
    return 0;
  }
  // subtract checksum
  *(data_len) -= CHECKSUM_SIZE;
  for (i = 0; i < hrp_len; ++i) {
    int ch = input[i];
    if (ch < 33 || ch > 126) {
      return 0;
    }
    if (ch >= 'a' && ch <= 'z') {
      have_lower = 1;
    } else if (ch >= 'A' && ch <= 'Z') {
      have_upper = 1;
      ch = (ch - 'A') + 'a';
    }
    hrp[i] = ch;
    chk = cashaddr_polymod_step(chk) ^ (ch & 0x1f);
  }
  hrp[i] = 0;
  chk = cashaddr_polymod_step(chk);
  ++i;
  while (i < input_len) {
    int v = (input[i] & 0x80) ? -1 : charset_rev[(int)input[i]];
    if (input[i] >= 'a' && input[i] <= 'z') have_lower = 1;
    if (input[i] >= 'A' && input[i] <= 'Z') have_upper = 1;
    if (v == -1) {
      return 0;
    }
    chk = cashaddr_polymod_step(chk) ^ v;
    if (i + CHECKSUM_SIZE < input_len) {
      data[i - (1 + hrp_len)] = v;
    }
    ++i;
  }
  if (have_lower && have_upper) {
    return 0;
  }
  return chk == 1;
}

static int convert_bits(uint8_t* out, size_t* outlen, int outbits,
                        const uint8_t* in, size_t inlen, int inbits, int pad) {
  uint32_t val = 0;
  int bits = 0;
  uint32_t maxv = (((uint32_t)1) << outbits) - 1;
  while (inlen--) {
    val = (val << inbits) | *(in++);
    bits += inbits;
    while (bits >= outbits) {
      bits -= outbits;
      out[(*outlen)++] = (val >> bits) & maxv;
    }
  }
  if (pad) {
    if (bits) {
      out[(*outlen)++] = (val << (outbits - bits)) & maxv;
    }
  } else if (((val << (outbits - bits)) & maxv) || bits >= inbits) {
    return 0;
  }
  return 1;
}

size_t cash_addr_decode(uint8_t* witdata, const char* hrp,
                     const char* addr) {
  uint8_t data[MAX_BASE32_SIZE] = {0};
  char hrp_actual[MAX_HRP_SIZE + 1] = {0};
  size_t data_len = 0;
  if (!cash_decode(hrp_actual, data, &data_len, addr)) return 0;
  if (data_len == 0 || data_len > MAX_BASE32_SIZE) return 0;
  if (strncmp(hrp, hrp_actual, MAX_HRP_SIZE + 1) != 0) return 0;
  size_t witdata_len = 0;
  if (!convert_bits(witdata, &witdata_len, 8, data, data_len, 5, 0)) return 0;
  if (witdata_len < 2 || witdata_len > MAX_DATA_SIZE) return 0;
  return witdata_len;
}
