/* Copyright (c) 2017 Jochen Hoenicke, Pieter Wuille
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

#ifndef _CASH_ADDR_H_
#define _CASH_ADDR_H_ 1

#include <stdint.h>

/** Decode a CashAddr address
 *
 *  Out: prog:     Pointer to a buffer of size 65 that will be updated to
 *                 contain the witness program bytes.
 *       returns length of bytes in prog. 
 * hrp:      Pointer to the null-terminated human
 * readable part that is expected (chain/network specific). addr:     Pointer to
 * the null-terminated address. Returns 1 if successful.
 */
size_t cash_addr_decode(uint8_t *prog, const char *hrp, const char *addr);

#endif
