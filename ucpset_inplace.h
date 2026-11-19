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
#ifndef UCPSET_INPLACE_H
#define UCPSET_INPLACE_H
/*
 * ucpset_inplace.h — in-place boolean set operations
 *
 * Requires x to have capacity for (xcount + ycount) intervals (or
 * xcount + 1 for complement).  y must not overlap x's buffer (y == x
 * itself is handled).
 *
 * After the fast paths, two strategies use that spare capacity:
 *
 *   - One interval: when either set is a single interval, the _interval
 *     operations of ucpset_merge.h rewrite only the affected run.
 *
 *   - Shift: move x up by ycount intervals, then run the forward sweep from
 *     ucpset_sweep.h, reading the moved x and writing from the start of the
 *     buffer.  This is the out-of-place loop, adaptive skipping included,
 *     at the cost of one block move.  The intervals of x that lie before y
 *     cannot change, so they stay where they are and only the rest is
 *     moved.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ucpset_internal.h"
#include "ucpset_merge.h"
#include "ucpset_search.h"
#include "ucpset_sweep.h"

// Move x up by `by` intervals, making room in front of it for a forward
// sweep to write into (see ucpset_sweep.h), and return the moved copy.
static inline const uint32_t* ucpset_inplace_shift_(uint32_t* x, size_t xcount,
                                                    size_t by) {
  uint32_t* moved = x + 2 * by;
  ucpset_move(moved, x, xcount);
  return moved;
}

static inline size_t ucpset_inplace_union(uint32_t* x, size_t xcount,
                                          const uint32_t* y, size_t ycount) {
  if (y == x || ycount == 0)
    return xcount;
  if (xcount == 0)
    return ucpset_copy(x, y, ycount);

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[2 * xcount - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[2 * ycount - 1];

  if (x_hi <= y_lo) {
    if (x_hi == y_lo) {
      x[2 * xcount - 1] = y[1];
      ucpset_copy(x + 2 * xcount, y + 2, ycount - 1);
      return xcount + ycount - 1;
    }
    ucpset_copy(x + 2 * xcount, y, ycount);
    return xcount + ycount;
  }
  if (y_hi <= x_lo) {
    ucpset_move(x + 2 * ycount, x, xcount);
    ucpset_copy(x, y, ycount);
    if (y_hi == x_lo) {
      x[2 * ycount - 1] = x[2 * ycount + 1];
      ucpset_move(x + 2 * ycount, x + 2 * ycount + 2, xcount - 1);
      return xcount + ycount - 1;
    }
    return xcount + ycount;
  }

  // One side is a single interval.
  if (ycount == 1)
    return ucpset_union_interval(x, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_union_interval(x, y, ycount, x_lo, x_hi);

  // The head of x that ends before y starts, with a gap, stays in place.
  const size_t h = ucpset_gallop_lower_bound_hi(x, xcount, y_lo);
  uint32_t* rest = x + 2 * h;
  const uint32_t* moved = ucpset_inplace_shift_(rest, xcount - h, ycount);
  return h + ucpset_sweep_union(rest, moved, xcount - h, y, ycount);
}

static inline size_t ucpset_inplace_intersection(uint32_t* x, size_t xcount,
                                                 const uint32_t* y, size_t ycount) {
  if (y == x)
    return xcount;
  if (xcount == 0 || ycount == 0)
    return 0;

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[2 * xcount - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[2 * ycount - 1];

  if (x_hi <= y_lo || y_hi <= x_lo)
    return 0;

  // One side is a single interval.
  if (ycount == 1)
    return ucpset_intersection_interval(x, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_intersection_interval(x, y, ycount, x_lo, x_hi);

  // Only the intervals of x that reach into y's span can contribute: the
  // head ending at or before y's start and the tail starting at or after
  // y's end are dropped.  The run in between is already far enough up when
  // the head is at least ycount intervals.
  const size_t h = ucpset_gallop_upper_bound_hi(x, xcount, y_lo);
  size_t e = h + ucpset_upper_bound_hi(x + 2 * h, xcount - h, y_hi);
  e += (e < xcount && x[2 * e] < y_hi);

  const uint32_t* run = x + 2 * h;
  if (h < ycount)
    run = ucpset_inplace_shift_(x + 2 * h, e - h, ycount - h);
  return ucpset_sweep_intersection(x, run, e - h, y, ycount);
}

static inline size_t ucpset_inplace_difference(uint32_t* x, size_t xcount,
                                               const uint32_t* y, size_t ycount) {
  if (y == x || xcount == 0)
    return 0;
  if (ycount == 0)
    return xcount;

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[2 * xcount - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[2 * ycount - 1];

  if (x_hi <= y_lo || y_hi <= x_lo)
    return xcount;

  // One side is a single interval.  [x_lo, x_hi) \ y is the complement of y
  // within [x_lo, x_hi); it reads only y and the two bounds.
  if (ycount == 1)
    return ucpset_difference_interval(x, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_complement(x, y, ycount, x_lo, x_hi);

  // The head of x that ends at or before y's start stays in place.
  const size_t h = ucpset_gallop_upper_bound_hi(x, xcount, y_lo);
  uint32_t* rest = x + 2 * h;
  const uint32_t* moved = ucpset_inplace_shift_(rest, xcount - h, ycount);
  return h + ucpset_sweep_difference(rest, moved, xcount - h, y, ycount);
}

static inline size_t ucpset_inplace_xor(uint32_t* x, size_t xcount,
                                        const uint32_t* y, size_t ycount) {
  if (y == x)
    return 0;
  if (xcount == 0)
    return ucpset_copy(x, y, ycount);
  if (ycount == 0)
    return xcount;

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[2 * xcount - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[2 * ycount - 1];

  if (x_hi <= y_lo) {
    if (x_hi == y_lo) {
      x[2 * xcount - 1] = y[1];
      ucpset_copy(x + 2 * xcount, y + 2, ycount - 1);
      return xcount + ycount - 1;
    }
    ucpset_copy(x + 2 * xcount, y, ycount);
    return xcount + ycount;
  }
  if (y_hi <= x_lo) {
    ucpset_move(x + 2 * ycount, x, xcount);
    ucpset_copy(x, y, ycount);
    if (y_hi == x_lo) {
      x[2 * ycount - 1] = x[2 * ycount + 1];
      ucpset_move(x + 2 * ycount, x + 2 * ycount + 2, xcount - 1);
      return xcount + ycount - 1;
    }
    return xcount + ycount;
  }

  // One side is a single interval.
  if (ycount == 1)
    return ucpset_xor_interval(x, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_xor_interval(x, y, ycount, x_lo, x_hi);

  // The head of x that ends before y starts, with a gap, stays in place.
  const size_t h = ucpset_gallop_lower_bound_hi(x, xcount, y_lo);
  uint32_t* rest = x + 2 * h;
  const uint32_t* moved = ucpset_inplace_shift_(rest, xcount - h, ycount);
  return h + ucpset_sweep_xor(rest, moved, xcount - h, y, ycount);
}

// x = [lo, hi) \ x.  hi is clamped to UCPSET_BOUND_MAX.
static inline size_t ucpset_inplace_complement(uint32_t* x, size_t xcount,
                                               uint32_t lo, uint32_t hi) {
  return ucpset_complement(x, x, xcount, lo, hi);
}

#endif /* UCPSET_INPLACE_H */
