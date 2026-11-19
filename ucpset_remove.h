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
#ifndef UCPSET_REMOVE_H
#define UCPSET_REMOVE_H
/*
 * ucpset_remove.h — in-place removal of elements and intervals
 *
 * Maintains strict canonical form.
 * Capacity: x must have space for (xcount + 1) intervals (split case).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "ucpset_internal.h"
#include "ucpset_search.h"

/*
 * Remove a single codepoint v from x.  May split an interval.
 * Returns new interval count.
 *
 * Values > UCPSET_CODEPOINT_MAX are no-ops.
 */
static inline size_t ucpset_remove(uint32_t* x, size_t xcount, uint32_t v) {
  if (v > UCPSET_CODEPOINT_MAX || xcount == 0)
    return xcount;

  size_t last_idx = (xcount - 1) * 2;

  // Fast path: value in or near last interval
  if (v >= x[last_idx]) {
    uint32_t lo = x[last_idx];
    uint32_t hi = x[last_idx + 1];
    if (v >= hi)
      return xcount;  // not in set
    if (lo + 1 == hi) {
      // Remove singleton interval
      return xcount - 1;
    }
    if (v == lo) {
      x[last_idx] = lo + 1;  //trim from left
      return xcount;
    }
    if (v + 1 == hi) {
      x[last_idx + 1] = v;  // trim from right
      return xcount;
    }
    // Split: [lo, v) and [v+1, hi)
    x[last_idx + 1] = v;
    x[last_idx + 2] = v + 1;
    x[last_idx + 3] = hi;
    return xcount + 1;
  }

  const size_t L = ucpset_upper_bound_hi(x, xcount, v);

  if (L >= xcount || v < x[2 * L])
    return xcount;  //not in set

  uint32_t lo = x[2 * L];
  uint32_t hi = x[2 * L + 1];

  if (lo + 1 == hi) {
    // Remove singleton interval
    return ucpset_open_run(x, xcount, L, L + 1, 0);
  }
  if (v == lo) {
    x[2 * L] = lo + 1;
    return xcount;
  }
  if (v + 1 == hi) {
    x[2 * L + 1] = v;
    return xcount;
  }

  // Split: [lo, v) and [v+1, hi)
  size_t n = ucpset_open_run(x, xcount, L, L + 1, 2);
  x[2 * L] = lo;
  x[2 * L + 1] = v;
  x[2 * (L + 1)] = v + 1;
  x[2 * (L + 1) + 1] = hi;
  return n;
}

/*
 * Remove interval [lo, hi) from x.  Binary search for the affected
 * intervals, then a single block move.  May split an interval.
 * Returns new interval count.
 *
 * No-op if lo >= hi.
 */
static inline size_t ucpset_remove_interval(uint32_t* x, size_t xcount,
                                            uint32_t lo, uint32_t hi) {
  if (!(lo < hi) || xcount == 0)
    return xcount;

  size_t last_idx = (xcount - 1) * 2;

  // Fast path: removal region starts at or after the last interval's lo
  if (lo >= x[last_idx]) {
    uint32_t x_lo = x[last_idx];
    uint32_t x_hi = x[last_idx + 1];

    if (hi <= x_lo || lo >= x_hi)
      return xcount;  // no overlap

    if (lo <= x_lo && hi >= x_hi)
      return xcount - 1;  // entire last interval removed

    if (lo <= x_lo && hi < x_hi) {
      x[last_idx] = hi;  // trim from left
      return xcount;
    }

    if (lo > x_lo && hi >= x_hi) {
      x[last_idx + 1] = lo;  // trim from right
      return xcount;
    }

    // Split: [x_lo, lo) and [hi, x_hi)
    x[last_idx + 1] = lo;
    x[last_idx + 2] = hi;
    x[last_idx + 3] = x_hi;
    return xcount + 1;
  }

  // General case.  lo is below the last interval's lo here, so some
  // interval ends after lo.

  // First interval ending after lo; nothing before it is affected.
  const size_t first = ucpset_upper_bound_hi(x, xcount, lo);
  if (x[2 * first] >= hi)
    return xcount;  // [lo, hi) falls in a gap

  // One past the last interval starting before hi.
  size_t end = first + ucpset_lower_bound_hi(x + 2 * first, xcount - first, hi);
  if (end < xcount && x[2 * end] < hi)
    end++;

  // Intervals [first, end) overlap the removal.  Only the outermost two can
  // leave a remnant: [left_lo, lo) on the left, [hi, right_hi) on the right.
  const uint32_t left_lo = x[2 * first];
  const uint32_t right_hi = x[2 * end - 1];
  const bool keep_left = left_lo < lo;
  const bool keep_right = right_hi > hi;

  // Close (or, when one interval splits in two, open) the gap in one move.
  const size_t n = ucpset_open_run(x, xcount, first, end,
                                   (size_t)keep_left + keep_right);

  size_t w = first;
  if (keep_left) {
    x[2 * w] = left_lo;
    x[2 * w + 1] = lo;
    w++;
  }
  if (keep_right) {
    x[2 * w] = hi;
    x[2 * w + 1] = right_hi;
  }

  return n;
}

#endif /* UCPSET_REMOVE_H */
