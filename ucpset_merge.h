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
#ifndef UCPSET_MERGE_H
#define UCPSET_MERGE_H
/*
 * ucpset_merge.h — out-of-place set operations
 *
 * Each function writes its result to a caller-provided buffer and returns
 * the number of intervals written.  Output is canonical.
 *
 *   - Two sets: union, intersection, difference, xor.  Fast paths for empty,
 *     non-overlapping and single-interval operands, then the per-operation
 *     forward sweep from ucpset_sweep.h.  dst needs room for
 *     (xcount + ycount) intervals and must not overlap x or y.
 *
 *   - A set and one interval: the _interval forms and complement.  These
 *     locate the affected run of x by search and rewrite only that run, so
 *     they cost a search plus block moves.  dst needs room for (xcount + 1)
 *     intervals and may be x itself, which makes the operation in place.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ucpset_internal.h"
#include "ucpset_search.h"
#include "ucpset_sweep.h"

/*
 * dst = [lo, hi) \ x: the part of the universe [lo, hi) not in x.
 *
 * The gaps between the intervals of x that overlap [lo, hi) are already in
 * x, as (hi, next lo) pairs, so the middle of the result is one block move.
 * hi is clamped to UCPSET_BOUND_MAX.  dst may be x.
 */
static inline size_t ucpset_complement(uint32_t* dst,
                                       const uint32_t* x, size_t xcount,
                                       uint32_t lo, uint32_t hi) {
  if (hi > UCPSET_BOUND_MAX)
    hi = UCPSET_BOUND_MAX;
  if (!(lo < hi))
    return 0;

  size_t i, j;
  ucpset_find_overlap_interval(x, xcount, lo, hi, &i, &j);

  if (i == j) {
    // Nothing of x inside [lo, hi).
    dst[0] = lo;
    dst[1] = hi;
    return 1;
  }

  const uint32_t first_lo = x[2 * i];
  const uint32_t last_hi = x[2 * j - 1];
  const size_t head = first_lo > lo;   // gap [lo, first_lo)
  const size_t mid = j - i - 1;        // gaps between the overlapping intervals

  // Middle first: when dst is x, writing the head could overwrite it.
  ucpset_move(dst + 2 * head, x + 2 * i + 1, mid);
  if (head) {
    dst[0] = lo;
    dst[1] = first_lo;
  }

  size_t k = 2 * (head + mid);
  if (last_hi < hi) {                  // gap [last_hi, hi)
    dst[k++] = last_hi;
    dst[k++] = hi;
  }
  return k / 2;
}

/*
 * dst = x ∪ [lo, hi): the intervals that overlap or touch [lo, hi) become
 * one interval.  hi is clamped to UCPSET_BOUND_MAX.  dst may be x.
 */
static inline size_t ucpset_union_interval(uint32_t* dst,
                                           const uint32_t* x, size_t xcount,
                                           uint32_t lo, uint32_t hi) {
  if (hi > UCPSET_BOUND_MAX)
    hi = UCPSET_BOUND_MAX;
  if (!(lo < hi))
    return ucpset_open_run_to(dst, x, xcount, xcount, xcount, 0);

  size_t i, j;
  ucpset_find_coalesce_interval(x, xcount, lo, hi, &i, &j);
  if (i < j) {
    lo = ucpset_min(lo, x[2 * i]);
    hi = ucpset_max(hi, x[2 * j - 1]);
  }

  size_t n = ucpset_open_run_to(dst, x, xcount, i, j, 1);
  dst[2 * i] = lo;
  dst[2 * i + 1] = hi;
  return n;
}

/*
 * dst = x ∩ [lo, hi): the overlapping intervals, the first and last cut
 * back to [lo, hi).  Needs room for xcount intervals only.  dst may be x.
 */
static inline size_t ucpset_intersection_interval(uint32_t* dst,
                                                  const uint32_t* x, size_t xcount,
                                                  uint32_t lo, uint32_t hi) {
  size_t i, j;
  ucpset_find_overlap_interval(x, xcount, lo, hi, &i, &j);

  const size_t k = j - i;
  if (k == 0)
    return 0;

  const uint32_t* run = x + 2 * i;
  const uint32_t first_lo = run[0];
  const uint32_t last_hi = run[2 * k - 1];

  if (dst != run)
    ucpset_move(dst, run, k);

  // Only the first interval can start before lo, only the last end after hi.
  if (first_lo < lo)
    dst[0] = lo;
  if (last_hi > hi)
    dst[2 * k - 1] = hi;
  return k;
}

/*
 * dst = x \ [lo, hi): the overlapping intervals leave at most a piece at
 * each end.  dst may be x.
 */
static inline size_t ucpset_difference_interval(uint32_t* dst,
                                                const uint32_t* x, size_t xcount,
                                                uint32_t lo, uint32_t hi) {
  size_t i, j;
  ucpset_find_overlap_interval(x, xcount, lo, hi, &i, &j);
  if (i == j)
    return ucpset_open_run_to(dst, x, xcount, xcount, xcount, 0);

  const uint32_t first_lo = x[2 * i];
  const uint32_t last_hi = x[2 * j - 1];
  const bool left = first_lo < lo;
  const bool right = last_hi > hi;

  size_t n = ucpset_open_run_to(dst, x, xcount, i, j, (size_t)left + right);
  uint32_t* p = dst + 2 * i;
  if (left) {
    *p++ = first_lo;
    *p++ = lo;
  }
  if (right) {
    *p++ = hi;
    *p++ = last_hi;
  }
  return n;
}

/*
 * dst = x △ [lo, hi): lo and hi are inserted among the endpoints of the
 * intervals that overlap or touch [lo, hi); an endpoint equal to either
 * cancels with it.  hi is clamped to UCPSET_BOUND_MAX.  dst may be x.
 */
static inline size_t ucpset_xor_interval(uint32_t* dst,
                                         const uint32_t* x, size_t xcount,
                                         uint32_t lo, uint32_t hi) {
  if (hi > UCPSET_BOUND_MAX)
    hi = UCPSET_BOUND_MAX;
  if (!(lo < hi))
    return ucpset_open_run_to(dst, x, xcount, xcount, xcount, 0);

  size_t i, j;
  ucpset_find_coalesce_interval(x, xcount, lo, hi, &i, &j);
  const uint32_t* b = x + 2 * i;       // endpoints of the run
  const size_t m = 2 * (j - i);

  // Front: an endpoint below lo, then lo unless it cancels.
  uint32_t pre[2];
  size_t p = 0;
  size_t start = 0;
  if (m && b[0] < lo)
    pre[p++] = b[start++];
  if (start < m && b[start] == lo)
    start++;
  else
    pre[p++] = lo;

  // Back: hi unless it cancels, then an endpoint above hi.
  uint32_t suf[2];
  size_t s = 0;
  size_t end = m;
  uint32_t above = 0;
  const bool has_above = m && b[m - 1] > hi;
  if (has_above)
    above = b[--end];
  if (end > start && b[end - 1] == hi)
    end--;
  else
    suf[s++] = hi;
  if (has_above)
    suf[s++] = above;

  // The run becomes pre + b[start, end) + suf.  When dst is x, move the
  // tail first if the run grows and the block first if it shrinks, so that
  // neither move overwrites what the other still has to read.
  const size_t block = end - start;
  const size_t width = p + block + s;  // endpoints in the rewritten run
  const size_t block_at = 2 * i + p;
  const bool tail_first = width > m || dst != x;
  size_t n = 0;

  if (tail_first)
    n = ucpset_open_run_to(dst, x, xcount, i, j, width / 2);
  if (block)
    memmove(dst + block_at, b + start, block * sizeof(uint32_t));
  if (!tail_first)
    n = ucpset_open_run_to(dst, x, xcount, i, j, width / 2);

  for (size_t t = 0; t < p; t++)
    dst[2 * i + t] = pre[t];
  for (size_t t = 0; t < s; t++)
    dst[block_at + block + t] = suf[t];
  return n;
}

static inline size_t ucpset_union(uint32_t* dst,
                                  const uint32_t* x, size_t xcount,
                                  const uint32_t* y, size_t ycount) {
  if (xcount == 0)
    return ucpset_copy(dst, y, ycount);

  if (ycount == 0)
    return ucpset_copy(dst, x, xcount);

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[xcount * 2 - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[ycount * 2 - 1];

  if (x_hi <= y_lo) {
    ucpset_copy(dst, x, xcount);
    if (x_hi == y_lo) {
      dst[xcount * 2 - 1] = y[1];
      ucpset_copy(dst + xcount * 2, y + 2, ycount - 1);
      return xcount + ycount - 1;
    }
    ucpset_copy(dst + xcount * 2, y, ycount);
    return xcount + ycount;
  }

  if (y_hi <= x_lo) {
    ucpset_copy(dst, y, ycount);
    if (y_hi == x_lo) {
      dst[ycount * 2 - 1] = x[1];
      ucpset_copy(dst + ycount * 2, x + 2, xcount - 1);
      return xcount + ycount - 1;
    }
    ucpset_copy(dst + ycount * 2, x, xcount);
    return xcount + ycount;
  }

  // One side is a single interval.
  if (ycount == 1)
    return ucpset_union_interval(dst, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_union_interval(dst, y, ycount, x_lo, x_hi);

  return ucpset_sweep_union(dst, x, xcount, y, ycount);
}

static inline size_t ucpset_intersection(uint32_t* dst,
                                         const uint32_t* x, size_t xcount,
                                         const uint32_t* y, size_t ycount) {
  if (xcount == 0 || ycount == 0)
    return 0;

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[xcount * 2 - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[ycount * 2 - 1];

  if (x_hi <= y_lo || y_hi <= x_lo)
    return 0;

  // One side is a single interval.
  if (ycount == 1)
    return ucpset_intersection_interval(dst, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_intersection_interval(dst, y, ycount, x_lo, x_hi);

  return ucpset_sweep_intersection(dst, x, xcount, y, ycount);
}

static inline size_t ucpset_difference(uint32_t* dst,
                                       const uint32_t* x, size_t xcount,
                                       const uint32_t* y, size_t ycount) {
  if (xcount == 0)
    return 0;

  if (ycount == 0)
    return ucpset_copy(dst, x, xcount);

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[xcount * 2 - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[ycount * 2 - 1];

  if (x_hi <= y_lo || y_hi <= x_lo)
    return ucpset_copy(dst, x, xcount);

  // One side is a single interval.  [x_lo, x_hi) \ y is the complement of y
  // within [x_lo, x_hi).
  if (ycount == 1)
    return ucpset_difference_interval(dst, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_complement(dst, y, ycount, x_lo, x_hi);

  return ucpset_sweep_difference(dst, x, xcount, y, ycount);
}

static inline size_t ucpset_xor(uint32_t* dst,
                                const uint32_t* x, size_t xcount,
                                const uint32_t* y, size_t ycount) {
  if (xcount == 0)
    return ucpset_copy(dst, y, ycount);

  if (ycount == 0)
    return ucpset_copy(dst, x, xcount);

  const uint32_t x_lo = x[0];
  const uint32_t x_hi = x[xcount * 2 - 1];
  const uint32_t y_lo = y[0];
  const uint32_t y_hi = y[ycount * 2 - 1];

  if (x_hi <= y_lo) {
    ucpset_copy(dst, x, xcount);
    if (x_hi == y_lo) {
      dst[xcount * 2 - 1] = y[1];
      ucpset_copy(dst + xcount * 2, y + 2, ycount - 1);
      return xcount + ycount - 1;
    }
    ucpset_copy(dst + xcount * 2, y, ycount);
    return xcount + ycount;
  }

  if (y_hi <= x_lo) {
    ucpset_copy(dst, y, ycount);
    if (y_hi == x_lo) {
      dst[ycount * 2 - 1] = x[1];
      ucpset_copy(dst + ycount * 2, x + 2, xcount - 1);
      return xcount + ycount - 1;
    }
    ucpset_copy(dst + ycount * 2, x, xcount);
    return xcount + ycount;
  }

  // One side is a single interval.
  if (ycount == 1)
    return ucpset_xor_interval(dst, x, xcount, y_lo, y_hi);
  if (xcount == 1)
    return ucpset_xor_interval(dst, y, ycount, x_lo, x_hi);

  return ucpset_sweep_xor(dst, x, xcount, y, ycount);
}

#endif /* UCPSET_MERGE_H */
