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
#ifndef UCPSET_PREDICATE_H
#define UCPSET_PREDICATE_H
/*
 * ucpset_predicate.h — set predicates and relational queries
 *
 * All predicates short-circuit where possible.  When one set is much larger
 * than the other, subset and disjoint skip through it adaptively (see
 * ucpset_search.h), so a small set against a large one costs far less than
 * a full sweep of the large one.  Sets of comparable size use a plain loop.
 * ucpset_pairwise_disjoint tests any number of sets at once.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ucpset_internal.h"
#include "ucpset_search.h"

/*
 * precedes: X sorts strictly before Y.
 *
 * For non-empty sets, every element of X is below every element of Y.
 * The empty set sorts first: it precedes every non-empty set, nothing
 * precedes it, and it does not precede itself.  This is a strict ordering,
 * not vacuous truth, and ucpset_precedes(X, Y) == ucpset_succeeds(Y, X).
 */
static inline bool ucpset_precedes(const uint32_t* x, size_t xcount,
                                   const uint32_t* y, size_t ycount) {
  if (xcount == 0)
    return ycount != 0;
  if (ycount == 0)
    return false;
  return x[xcount * 2 - 1] <= y[0];
}

/*
 * succeeds: X sorts strictly after Y.
 *
 * For non-empty sets, every element of X is above every element of Y.
 * The empty set sorts first: every non-empty set succeeds it, it succeeds
 * nothing, and it does not succeed itself.
 */
static inline bool ucpset_succeeds(const uint32_t* x, size_t xcount,
                                   const uint32_t* y, size_t ycount) {
  if (xcount == 0)
    return false;
  if (ycount == 0)
    return true;
  return y[ycount * 2 - 1] <= x[0];
}

// subset: X ⊆ Y.
static inline bool ucpset_subset(const uint32_t* x, size_t xcount,
                                 const uint32_t* y, size_t ycount) {
  if (xcount == 0)
    return true;
  if (ycount == 0)
    return false;

  uint32_t x_lo = x[0];
  uint32_t x_hi = x[xcount * 2 - 1];
  uint32_t y_lo = y[0];
  uint32_t y_hi = y[ycount * 2 - 1];

  if (x_lo < y_lo || x_hi > y_hi)
    return false;

  // Every x interval must lie within a single y interval (y is canonical,
  // so two adjacent intervals cannot cover one).  y's last interval ends at
  // or after x's last (checked above), so j cannot run past the end of y.
  const size_t xn = 2 * xcount, yn = 2 * ycount;
  size_t j = 0;

  if (!ucpset_much_larger_(ycount, xcount)) {
    // Comparable sizes: step through y.
    for (size_t i = 0; i < xn; i += 2) {
      // First y interval ending at or after this x interval's end.
      while (y[j + 1] < x[i + 1])
        j += 2;
      if (y[j] > x[i])
        return false;
    }
    return true;
  }

  // y is much larger than x: skip through it adaptively.
  for (size_t i = 0; i < xn; i += 2) {
    const uint32_t x_lo = x[i];
    const uint32_t x_hi = x[i + 1];

    // First y interval ending at or after this x interval's end
    // (hi >= x_hi, i.e. hi > x_hi - 1; x_hi is at least 1).
    unsigned run = 0;
    while (y[j + 1] < x_hi)
      j = ucpset_skip_hi_(y, yn, j, x_hi - 1, &run);

    if (y[j] > x_lo)
      return false;
  }
  return true;
}

// superset: X ⊇ Y.
static inline bool ucpset_superset(const uint32_t* x, size_t xcount,
                                   const uint32_t* y, size_t ycount) {
  return ucpset_subset(y, ycount, x, xcount);
}

// disjoint: X ∩ Y = ∅.
static inline bool ucpset_disjoint(const uint32_t* x, size_t xcount,
                                   const uint32_t* y, size_t ycount) {
  if (xcount == 0 || ycount == 0)
    return true;

  uint32_t x_lo = x[0];
  uint32_t x_hi = x[xcount * 2 - 1];
  uint32_t y_lo = y[0];
  uint32_t y_hi = y[ycount * 2 - 1];

  if (x_hi <= y_lo || y_hi <= x_lo)
    return true;

  // Step past whichever interval ends first until a pair overlaps.
  const size_t xn = 2 * xcount, yn = 2 * ycount;
  size_t i = 0, j = 0;

  if (!ucpset_much_larger_(xcount, ycount) &&
      !ucpset_much_larger_(ycount, xcount)) {
    // Comparable sizes: plain stepping.
    while (i < xn && j < yn) {
      if (x[i + 1] <= y[j])
        i += 2;
      else if (y[j + 1] <= x[i])
        j += 2;
      else
        return false;
    }
    return true;
  }

  // One set is much larger: skip through it adaptively.
  unsigned run_x = 0, run_y = 0;
  while (i < xn && j < yn) {
    if (x[i + 1] <= y[j]) {
      run_y = 0;
      i = ucpset_skip_hi_(x, xn, i, y[j], &run_x);
    } else if (y[j + 1] <= x[i]) {
      run_x = 0;
      j = ucpset_skip_hi_(y, yn, j, x[i], &run_y);
    } else {
      return false;
    }
  }
  return true;
}

/*
 * pairwise disjoint: no two of the k sets share a codepoint.
 *
 * sets[t] is a canonical set of counts[t] intervals; an empty set may be
 * given as NULL.  scratch is working space for 2 * k entries; its contents
 * on return are unspecified.  The same set given twice is not disjoint from
 * itself, unless it is empty.
 *
 * The sets are walked together in codepoint order.  The set whose next
 * interval starts first skips ahead to where the next set starts, and
 * overlaps it exactly when one of its intervals reaches across that point.
 *
 * The cost follows how the sets interleave.  Sets that come in long runs,
 * such as scripts, cost one search per run: far less than testing every
 * pair.  Sets that alternate interval by interval cost about as much as
 * testing every pair.
 *
 * In C an array of uint32_t* does not convert to const uint32_t* const*
 * without a cast; declare the array as const uint32_t* sets[].
 */
static inline bool ucpset_pairwise_disjoint(const uint32_t* const* sets,
                                            const size_t* counts, size_t k,
                                            size_t* scratch) {
  if (k == 2)
    return ucpset_disjoint(sets[0], counts[0], sets[1], counts[1]);

  size_t* pos = scratch;        // next unread interval of each set
  size_t* order = scratch + k;  // sets with intervals left, by head start
  size_t live = 0;

  // Insertion sort of the non-empty sets by the start of their first interval.
  for (size_t t = 0; t < k; t++) {
    pos[t] = 0;
    if (counts[t] == 0)
      continue;
    const uint32_t lo = sets[t][0];
    size_t j = live++;
    for (; j > 0 && sets[order[j - 1]][0] > lo; j--)
      order[j] = order[j - 1];
    order[j] = t;
  }

  while (live >= 2) {
    // a has the lowest head start; m is the next lowest, at or after it.
    const size_t a = order[0];
    const size_t b = order[1];
    const uint32_t m = sets[b][2 * pos[b]];
    const uint32_t* x = sets[a];
    const size_t n = counts[a];
    size_t i = pos[a];

    // Skip a's intervals ending at or before m: nothing else is there yet.
    // One step first; the gallop is for a run that continues.
    if (x[2 * i + 1] <= m) {
      i++;
      if (i < n && x[2 * i + 1] <= m)
        i += ucpset_gallop_upper_bound_hi(x + 2 * i, n - i, m);
    }

    // The interval now at i ends after m; starting at or before m, it
    // overlaps the interval of b that starts at m.
    if (i < n && x[2 * i] <= m)
      return false;
    pos[a] = i;

    // Put a back in order, or drop it when it has run out.
    size_t j = 1;
    if (i < n) {
      const uint32_t lo = x[2 * i];
      for (; j < live && sets[order[j]][2 * pos[order[j]]] < lo; j++)
        order[j - 1] = order[j];
      order[j - 1] = a;
    } else {
      for (; j < live; j++)
        order[j - 1] = order[j];
      live--;
    }
  }
  return true;
}

// equal: X = Y.
static inline bool ucpset_equal(const uint32_t* x, size_t xcount,
                                const uint32_t* y, size_t ycount) {
  if (x == y && xcount == ycount)
    return true;
  if (xcount != ycount)
    return false;
  if (xcount == 0)
    return true;
  return memcmp(x, y, xcount * 2 * sizeof(uint32_t)) == 0;
}

#endif /* UCPSET_PREDICATE_H */
