/*
 * Copyright (c) 2026 Christian Hansen <chansen@cpan.org>
 * <https://github.com/chansen/c-ucpset>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef UCPSET_INTERNAL_H
#define UCPSET_INTERNAL_H
/*
 * ucpset_internal.h — constants and small helpers shared by the headers
 *
 * The Unicode bounds, min/max, and block moves of intervals.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Keeps a function out of line.  Used for the large merge loops: compiled
 * on their own they come out the same wherever they are called from, where
 * inlined into a caller their layout, and with it their speed, varied with
 * the code around them.
 */
#if defined(__GNUC__) || defined(__clang__)
#define UCPSET_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define UCPSET_NOINLINE __declspec(noinline)
#else
#define UCPSET_NOINLINE
#endif

#define UCPSET_CODEPOINT_MAX  0x10FFFF   // maximum valid Unicode code point
#define UCPSET_BOUND_MAX      0x110000   // exclusive upper bound

static inline uint32_t ucpset_min(uint32_t a, uint32_t b) {
  return a < b ? a : b;
}

static inline uint32_t ucpset_max(uint32_t a, uint32_t b) {
  return a > b ? a : b;
}

/*
 * Copy n intervals from src to dst.  The two regions must not overlap.
 * Returns n.
 */
static inline size_t ucpset_copy(uint32_t* dst, const uint32_t* src, size_t n) {
  if (n)
    memcpy(dst, src, 2 * n * sizeof(uint32_t));
  return n;
}

/*
 * Move n intervals from src to dst.  The two regions may overlap.
 * Returns n.
 */
static inline size_t ucpset_move(uint32_t* dst, const uint32_t* src, size_t n) {
  if (n && dst != src)
    memmove(dst, src, 2 * n * sizeof(uint32_t));
  return n;
}

/*
 * Replace intervals [i, j) of x with k unwritten intervals, in place:
 * x = x[0, i) + k free slots + x[j, xcount).  Returns the new count,
 * xcount - (j - i) + k.  The caller fills in the k slots at x + 2 * i.
 */
static inline size_t ucpset_open_run(uint32_t* x, size_t xcount,
                                     size_t i, size_t j, size_t k) {
  return i + k + ucpset_move(x + 2 * (i + k), x + 2 * j, xcount - j);
}

/*
 * As ucpset_open_run, writing to dst:
 * dst = x[0, i) + k free slots + x[j, xcount).  dst is x itself or does not
 * overlap it.
 */
static inline size_t ucpset_open_run_to(uint32_t* dst, const uint32_t* x,
                                        size_t xcount, size_t i, size_t j,
                                        size_t k) {
  if (dst != x)
    ucpset_copy(dst, x, i);
  return i + k + ucpset_move(dst + 2 * (i + k), x + 2 * j, xcount - j);
}

#endif /* UCPSET_INTERNAL_H */
