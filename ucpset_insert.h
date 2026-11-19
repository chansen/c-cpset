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
#ifndef UCPSET_INSERT_H
#define UCPSET_INSERT_H
/*
 * ucpset_insert.h — in-place insertion of elements and intervals
 *
 * Maintains strict canonical form: sorted, disjoint, non-adjacent.
 * Capacity: x must have space for (xcount + 1) intervals.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "ucpset_internal.h"
#include "ucpset_search.h"

/*
 * Insert a single codepoint v into x.  Equivalent to union with [v, v+1).
 * Returns new interval count.
 */
static inline size_t ucpset_insert(uint32_t* x, size_t xcount, uint32_t v) {
  if (v > UCPSET_CODEPOINT_MAX)
    return xcount;

  if (xcount == 0) {
    x[0] = v;
    x[1] = v + 1;
    return 1;
  }

  size_t last_idx = (xcount - 1) * 2;

  // Fast path: value at or beyond last interval
  if (v >= x[last_idx]) {
    uint32_t last_hi = x[last_idx + 1];
    if (v < last_hi)
      return xcount;  // already contained
    if (v == last_hi) {
      x[last_idx + 1]++;
      return xcount;
    }
    // Append new interval
    x[last_idx + 2] = v;
    x[last_idx + 3] = v + 1;
    return xcount + 1;
  }

  const size_t L = ucpset_upper_bound_hi(x, xcount, v);

  // Already contained?
  if (L < xcount && x[2 * L] <= v)
    return xcount;

  bool merge_left  = (L > 0 && x[2 * (L - 1) + 1] == v);
  bool merge_right = (L < xcount && x[2 * L] == v + 1);

  if (merge_left && merge_right) {
    // Bridge two intervals: extend left to cover right, remove right.
    x[2 * (L - 1) + 1] = x[2 * L + 1];
    return ucpset_open_run(x, xcount, L, L + 1, 0);
  }

  if (merge_left) {
    x[2 * (L - 1) + 1] = v + 1;
    return xcount;
  }

  if (merge_right) {
    x[2 * L] = v;
    return xcount;
  }

  // Insert new singleton interval [v, v+1) at position L.
  size_t n = ucpset_open_run(x, xcount, L, L, 1);
  x[2 * L] = v;
  x[2 * L + 1] = v + 1;
  return n;
}

/*
 * Insert interval [lo, hi) into x.  Merges overlapping and adjacent intervals.
 * Returns new interval count.
 *
 * hi is clamped to UCPSET_BOUND_MAX; the part of the interval above
 * U+10FFFF is ignored, as ucpset_insert ignores such values.
 */
static inline size_t ucpset_insert_interval(uint32_t* x, size_t xcount,
                                            uint32_t lo, uint32_t hi) {
  if (hi > UCPSET_BOUND_MAX)
    hi = UCPSET_BOUND_MAX;
  if (!(lo < hi))
    return xcount;

  if (xcount == 0) {
    x[0] = lo;
    x[1] = hi;
    return 1;
  }

  size_t last_idx = (xcount - 1) * 2;

  // Fast path: interval starts at or beyond last interval
  if (lo >= x[last_idx]) {
    uint32_t last_hi = x[last_idx + 1];
    if (lo <= last_hi) {
      if (hi > last_hi)
        x[last_idx + 1] = hi;
      return xcount;
    }
    // Append new interval
    x[last_idx + 2] = lo;
    x[last_idx + 3] = hi;
    return xcount + 1;
  }

  const size_t L = ucpset_lower_bound_hi(x, xcount, lo);

  if (L < xcount && x[2 * L] <= hi) {
    // Merge overlapping/adjacent intervals [L, j).
    lo = ucpset_min(lo, x[2 * L]);
    size_t j = L;
    while (j < xcount && x[2 * j] <= hi) {
      hi = ucpset_max(hi, x[2 * j + 1]);
      j++;
    }
    size_t n = ucpset_open_run(x, xcount, L, j, 1);
    x[2 * L] = lo;
    x[2 * L + 1] = hi;
    return n;
  }

  // No overlap.
  size_t n = ucpset_open_run(x, xcount, L, L, 1);
  x[2 * L] = lo;
  x[2 * L + 1] = hi;
  return n;
}

#endif /* UCPSET_INSERT_H */
