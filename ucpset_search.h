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
#ifndef UCPSET_SEARCH_H
#define UCPSET_SEARCH_H
/*
 * ucpset_search.h — locating intervals in a set
 *
 *   - ucpset_upper_bound_hi, ucpset_lower_bound_hi: branch-free binary
 *     search on the hi endpoints.
 *   - ucpset_gallop_upper_bound_hi, ucpset_gallop_lower_bound_hi: the same
 *     results by exponential search from the front.
 *   - ucpset_find, ucpset_find_overlap_interval,
 *     ucpset_find_coalesce_interval: the interval holding a codepoint, and
 *     the run of intervals an interval overlaps or touches.
 *
 * All of them return interval indices in [0, xcount].
 *
 * Also here, because the merge loops and the predicates share them:
 * UCPSET_GALLOP_MIN, the adaptive skip step, and the size test that decides
 * whether adaptive skipping is worth its bookkeeping.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ucpset_upper_bound_hi — find first interval whose hi > key.
 *
 * Returns i in [0, xcount] such that:
 *   - for all j < i:  x[2*j+1] <= key
 *   - for all j >= i: x[2*j+1] > key
 */
static inline size_t ucpset_upper_bound_hi(const uint32_t* x,
                                           size_t xcount,
                                           uint32_t key) {
  if (xcount == 0)
    return 0;
  if (key < x[1])
    return 0;
  if (key >= x[2 * (xcount - 1) + 1])
    return xcount;

  size_t L = 0;
  size_t len = xcount;
  while (len > 1) {
    size_t half = len / 2;
    size_t mid = L + half - 1;
    size_t step = (x[2 * mid + 1] <= key);
    L += half * step;
    len -= half;
  }
  return L + (x[2 * L + 1] <= key);
}

/*
 * ucpset_lower_bound_hi — find first interval whose hi >= key.
 *
 * Returns i in [0, xcount] such that:
 *   - for all j < i:  x[2*j+1] < key
 *   - for all j >= i: x[2*j+1] >= key
 */
static inline size_t ucpset_lower_bound_hi(const uint32_t* x,
                                           size_t xcount,
                                           uint32_t key) {
  if (xcount == 0)
    return 0;
  if (key <= x[1])
    return 0;
  if (key > x[2 * (xcount - 1) + 1])
    return xcount;

  size_t L = 0;
  size_t len = xcount;
  while (len > 1) {
    size_t half = len / 2;
    size_t mid = L + half - 1;
    size_t step = (x[2 * mid + 1] < key);
    L += half * step;
    len -= half;
  }
  return L + (x[2 * L + 1] < key);
}

/*
 * Adaptive skipping for the sweep loops.
 *
 * A sweep normally steps one interval at a time.  When one set is much
 * sparser than the other in some region, the denser side takes many steps
 * in a row; after UCPSET_GALLOP_MIN of them it gallops instead.  Stepping
 * stays the default because it is faster when the sets interleave closely.
 *
 * Define UCPSET_GALLOP_MIN before including to tune it; 1 gallops on every
 * step (used by the tests to reach the gallop paths with tiny sets).
 */
#ifndef UCPSET_GALLOP_MIN
#define UCPSET_GALLOP_MIN 8
#endif

/*
 * ucpset_gallop_upper_bound_hi — as ucpset_upper_bound_hi, by exponential
 * search from the front: probe intervals 1, 2, 4, ... ahead, then binary
 * search the bracket.  Costs O(log result) rather than O(log xcount), so it
 * is cheap when the answer is near the start.
 */
static inline size_t ucpset_gallop_upper_bound_hi(const uint32_t* x,
                                                  size_t xcount,
                                                  uint32_t key) {
  size_t base = 0;
  size_t step = 1;
  while (base + step <= xcount && x[2 * (base + step - 1) + 1] <= key) {
    base += step;
    step *= 2;
  }

  // Intervals before base end at or before key.  The answer is among the
  // next step - 1 intervals, or among whatever is left of the array.
  size_t len = (base + step <= xcount) ? step - 1 : xcount - base;
  return base + ucpset_upper_bound_hi(x + 2 * base, len, key);
}

/*
 * ucpset_gallop_lower_bound_hi — as ucpset_lower_bound_hi, by exponential
 * search from the front, like ucpset_gallop_upper_bound_hi.
 */
static inline size_t ucpset_gallop_lower_bound_hi(const uint32_t* x,
                                                  size_t xcount,
                                                  uint32_t key) {
  size_t base = 0;
  size_t step = 1;
  while (base + step <= xcount && x[2 * (base + step - 1) + 1] < key) {
    base += step;
    step *= 2;
  }

  size_t len = (base + step <= xcount) ? step - 1 : xcount - base;
  return base + ucpset_lower_bound_hi(x + 2 * base, len, key);
}

/*
 * ucpset_find — index of the interval containing codepoint v, or xcount if
 * v is not in x.
 *
 * Time: O(1) if v is below the end of the first interval or at/after the
 * end of the last, O(log n) otherwise.
 */
static inline size_t ucpset_find(const uint32_t* x, size_t xcount, uint32_t v) {
  if (xcount == 0)
    return 0;
  if (v < x[1])
    return v >= x[0] ? 0 : xcount;

  // First interval ending after v, if any; it holds v iff it starts at or
  // before v.
  size_t L = ucpset_upper_bound_hi(x, xcount, v);
  return (L < xcount && v >= x[2 * L]) ? L : xcount;
}

/*
 * ucpset_find_overlap_interval — the intervals of x that share at least one
 * codepoint with [lo, hi) are exactly [*i, *j).
 *
 * Only the first of them can start before lo and only the last can end
 * after hi.  An empty [lo, hi) (lo >= hi) overlaps nothing: *i == *j.
 *
 * Time: O(1) if lo is at or beyond the last interval's start, O(log n)
 * otherwise.
 */
static inline void ucpset_find_overlap_interval(const uint32_t* x,
                                                size_t xcount,
                                                uint32_t lo, uint32_t hi,
                                                size_t* i, size_t* j) {
  if (xcount == 0) {
    *i = *j = 0;
    return;
  }

  const size_t last = 2 * (xcount - 1);

  // Only the last interval can overlap.
  if (lo >= x[last]) {
    size_t L = lo < x[last + 1] ? xcount - 1 : xcount;
    *i = L;
    *j = L + (L < xcount && lo < hi);
    return;
  }

  // First interval ending after lo: everything before it ends at or
  // before lo.
  size_t L = ucpset_upper_bound_hi(x, xcount, lo);
  size_t R = L;
  if (lo < hi) {
    // First interval ending at or after hi.  It also ends after lo, so it
    // is at or after L; it overlaps only if it starts before hi.
    R = L + ucpset_gallop_lower_bound_hi(x + 2 * L, xcount - L, hi);
    R += (R < xcount && x[2 * R] < hi);
  }
  *i = L;
  *j = R;
}

/*
 * ucpset_find_coalesce_interval — the intervals of x that overlap or touch
 * [lo, hi) are exactly [*i, *j): the ones a union with [lo, hi) merges into
 * a single interval.
 *
 * When none do, *i == *j; for a non-empty [lo, hi) that is the position at
 * which it would be inserted.  An empty [lo, hi) (lo >= hi) coalesces with
 * nothing.
 *
 * Time: O(1) if lo is at or beyond the last interval's start, O(log n)
 * otherwise.
 */
static inline void ucpset_find_coalesce_interval(const uint32_t* x,
                                                 size_t xcount,
                                                 uint32_t lo, uint32_t hi,
                                                 size_t* i, size_t* j) {
  if (xcount == 0) {
    *i = *j = 0;
    return;
  }

  const size_t last = 2 * (xcount - 1);

  // Only the last interval can coalesce.
  if (lo >= x[last]) {
    size_t L = lo <= x[last + 1] ? xcount - 1 : xcount;
    *i = L;
    *j = L + (L < xcount && lo < hi);
    return;
  }

  // First interval ending at or after lo: everything before it ends
  // strictly before lo.
  size_t L = ucpset_lower_bound_hi(x, xcount, lo);
  size_t R = L;
  if (lo < hi) {
    // First interval ending at or after hi, at or after L; it coalesces if
    // it starts at or before hi.
    R = L + ucpset_gallop_lower_bound_hi(x + 2 * L, xcount - L, hi);
    R += (R < xcount && x[2 * R] <= hi);
  }
  *i = L;
  *j = R;
}

/*
 * One step of an adaptive skip.  The caller has found that the interval at
 * element offset i of x ends at or before key, and wants the next interval
 * that does not.  Steps past it; once *run consecutive steps reach
 * UCPSET_GALLOP_MIN, also gallops past every further such interval.
 *
 * xn and i are array-element offsets (two per interval).  Returns the new
 * offset.  The caller resets *run whenever the run is broken.
 */
static inline size_t ucpset_skip_hi_(const uint32_t* x, size_t xn, size_t i,
                                     uint32_t key, unsigned* run) {
  i += 2;
  if (++*run >= UCPSET_GALLOP_MIN) {
    *run = 0;
    i += 2 * ucpset_gallop_upper_bound_hi(x + i, (xn - i) / 2, key);
  }
  return i;
}

/*
 * True when a set of `big` intervals is enough larger than one of `small`
 * intervals for adaptive skipping over it to pay.
 *
 * Stepping through both sets, the larger one takes about big / small steps
 * for each step of the smaller.  Unless that is well past UCPSET_GALLOP_MIN,
 * runs rarely get long enough to gallop and counting them only slows the
 * loop, so sets of comparable size are better served by a plain loop.
 */
static inline bool ucpset_much_larger_(size_t big, size_t small) {
  return big / (2 * UCPSET_GALLOP_MIN) >= small;
}

#endif /* UCPSET_SEARCH_H */
