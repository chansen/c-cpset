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
#ifndef UCPSET_MEMBERSHIP_H
#define UCPSET_MEMBERSHIP_H
/*
 * ucpset_membership.h — membership queries
 *
 * Test whether a single codepoint or an entire interval is in the set.
 * ucpset_contains_hint is ucpset_contains for codepoints that arrive in
 * text order, where each one tends to fall near the one before it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ucpset_search.h"

/*
 * Test if codepoint v is in the set.
 *
 * Time: O(1) if v is below the end of the first interval or at/after the
 * end of the last, O(log n) otherwise.
 */
static inline bool ucpset_contains(const uint32_t* x, size_t xcount, uint32_t v) {
  if (xcount == 0)
    return false;

  // Below the end of the first interval: decided without a search.  This is
  // the common case for low code points against a set that starts higher.
  if (v < x[1])
    return v >= x[0];

  // First interval ending after v, if any; v is in the set iff it starts
  // at or before v.
  size_t L = ucpset_upper_bound_hi(x, xcount, v);
  return L < xcount && v >= x[2 * L];
}

/*
 * Test if codepoint v is in the set, starting from where an earlier lookup
 * ended.  Returns what ucpset_contains returns.
 *
 * *hint is the caller's: set it to 0 before the first lookup and pass the
 * same variable to the following ones.  It holds an interval index, but
 * any value is accepted.  A hint that is off, or left over from before the
 * set was edited, costs time and never a wrong answer.  Use one hint for
 * each set and each stream of lookups.
 *
 * Time: O(1) when v is below the end of the first interval, or in or next
 * to the interval the hint points at; otherwise O(log d), where d is how
 * many intervals away v lies.  Codepoints in arbitrary order are slower
 * this way than with ucpset_contains.
 */
static inline bool ucpset_contains_hint(const uint32_t* x, size_t xcount,
                                        uint32_t v, size_t* hint) {
  if (xcount == 0)
    return false;

  // Below the end of the first interval: decided without a search, as in
  // ucpset_contains.  The hint stays where it is, so the spaces and ASCII
  // punctuation between words do not pull it away from the script the
  // words are in.
  if (v < x[1])
    return v >= x[0];

  // i becomes the first interval ending after v, or xcount.
  size_t i = *hint;
  if (i > xcount)
    i = xcount;

  if (i < xcount && x[2 * i + 1] <= v) {
    // Interval i ends at or before v: search forward from the next one.
    i++;
    i += ucpset_gallop_upper_bound_hi(x + 2 * i, xcount - i, v);
  } else if (x[2 * i - 1] > v) {
    // Interval i - 1 ends after v too: search backward.  (i is not 0 here:
    // the first interval ends at or before v, so a hint of 0 went forward.)
    // Intervals from top on end after v; probe 1, 2, 4, ... further back
    // until one does not, then binary search the bracket.
    size_t top = i - 1;
    size_t step = 1;
    while (step <= top && x[2 * (top - step) + 1] > v) {
      top -= step;
      step *= 2;
    }
    size_t base = (step <= top) ? top - step + 1 : 0;
    i = base + ucpset_upper_bound_hi(x + 2 * base, top - base, v);
  }

  *hint = i;
  return i < xcount && v >= x[2 * i];
}

/*
 * Test if the entire interval [lo, hi) is covered by the set.
 *
 * Returns true for empty intervals (lo >= hi): the empty interval is a
 * subset of every set, including the empty set (as ucpset_subset).
 */
static inline bool ucpset_contains_interval(const uint32_t* x,
                                            size_t xcount,
                                            uint32_t lo,
                                            uint32_t hi) {
  if (!(lo < hi))
    return true;
  if (xcount == 0)
    return false;

  // Starts below the end of the first interval: decided without a search.
  if (lo < x[1])
    return lo >= x[0] && hi <= x[1];

  // First interval ending after lo, if any; it must cover all of [lo, hi).
  size_t L = ucpset_upper_bound_hi(x, xcount, lo);
  return L < xcount && lo >= x[2 * L] && hi <= x[2 * L + 1];
}

#endif /* UCPSET_MEMBERSHIP_H */
