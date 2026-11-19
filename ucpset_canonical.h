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
#ifndef UCPSET_CANONICAL_H
#define UCPSET_CANONICAL_H
/*
 * ucpset_canonical.h — canonical form: check it (ucpset_canonical) and
 * establish it (ucpset_normalize, ucpset_normalize_codepoints).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ucpset_internal.h"

/*
 * Check that x is strictly canonical:
 *   - Every interval has lo < hi.
 *   - Consecutive intervals satisfy prev_hi < next_lo (non-adjacent).
 *   - All endpoints are within Unicode bounds (hi <= UCPSET_BOUND_MAX).
 *
 * Empty sets (xcount == 0) are canonical.
 */
static inline bool ucpset_canonical(const uint32_t* x, size_t xcount) {
  if (xcount == 0)
    return true;

  for (size_t i = 0; i < xcount; i++) {
    uint32_t lo = x[i * 2];
    uint32_t hi = x[i * 2 + 1];

    if (!(lo < hi))
      return false;
    if (hi > UCPSET_BOUND_MAX)
      return false;

    if (i + 1 < xcount) {
      uint32_t next_lo = x[(i + 1) * 2];
      if (!(hi < next_lo))
        return false;
    }
  }

  return true;
}

// In-place sort of n intervals by lo: introsort (quicksort with insertion
// sort for short ranges and heapsort past the depth limit), no callbacks.
static inline void ucpset_swap_(uint32_t* x, size_t i, size_t j) {
  uint32_t lo = x[2 * i];
  uint32_t hi = x[2 * i + 1];
  x[2 * i] = x[2 * j];
  x[2 * i + 1] = x[2 * j + 1];
  x[2 * j] = lo;
  x[2 * j + 1] = hi;
}

static inline void ucpset_sift_(uint32_t* x, size_t root, size_t n) {
  for (;;) {
    size_t c = 2 * root + 1;
    if (c >= n)
      return;
    if (c + 1 < n && x[2 * c + 2] > x[2 * c])
      c++;
    if (x[2 * root] >= x[2 * c])
      return;
    ucpset_swap_(x, root, c);
    root = c;
  }
}

static inline void ucpset_sort_(uint32_t* x, size_t n, unsigned depth) {
  while (n > 16) {
    if (depth-- == 0) {
      for (size_t i = n / 2; i-- > 0;)
        ucpset_sift_(x, i, n);
      for (size_t i = n - 1; i > 0; i--) {
        ucpset_swap_(x, 0, i);
        ucpset_sift_(x, 0, i);
      }
      return;
    }
    // Median of three into position 0 as the pivot.
    size_t m = n / 2;
    if (x[2 * m] < x[0])
      ucpset_swap_(x, m, 0);
    if (x[2 * (n - 1)] < x[0])
      ucpset_swap_(x, n - 1, 0);
    if (x[2 * (n - 1)] < x[2 * m])
      ucpset_swap_(x, n - 1, m);
    ucpset_swap_(x, 0, m);
    uint32_t pivot = x[0];

    size_t i = 1;
    size_t j = n - 1;
    for (;;) {
      while (x[2 * i] < pivot)
        i++;
      while (x[2 * j] > pivot)
        j--;
      if (i >= j)
        break;
      ucpset_swap_(x, i, j);
      i++;
      j--;
    }
    ucpset_swap_(x, 0, j);

    // Recurse into the smaller side, loop on the larger.
    if (j < n - j - 1) {
      ucpset_sort_(x, j, depth);
      x += 2 * (j + 1);
      n -= j + 1;
    } else {
      ucpset_sort_(x + 2 * (j + 1), n - j - 1, depth);
      n = j;
    }
  }
  for (size_t i = 1; i < n; i++) {
    uint32_t lo = x[2 * i];
    uint32_t hi = x[2 * i + 1];
    size_t k = i;
    for (; k > 0 && x[2 * k - 2] > lo; k--) {
      x[2 * k] = x[2 * k - 2];
      x[2 * k + 1] = x[2 * k - 1];
    }
    x[2 * k] = lo;
    x[2 * k + 1] = hi;
  }
}

/*
 * Make x canonical in place: any order, overlapping, adjacent, empty or
 * reversed intervals.  hi is clamped to UCPSET_BOUND_MAX and empty intervals
 * are dropped.  Returns the new count.  O(n) if x is already sorted by lo,
 * O(n log n) otherwise.
 */
static inline size_t ucpset_normalize(uint32_t* x, size_t xcount) {
  // Clamp, drop empty intervals, and note whether lo is already sorted.
  size_t n = 0;
  bool sorted = true;
  for (size_t i = 0; i < xcount; i++) {
    uint32_t lo = x[2 * i];
    uint32_t hi = ucpset_min(x[2 * i + 1], UCPSET_BOUND_MAX);
    if (!(lo < hi))
      continue;
    if (n > 0 && lo < x[2 * n - 2])
      sorted = false;
    x[2 * n] = lo;
    x[2 * n + 1] = hi;
    n++;
  }
  if (!sorted) {
    unsigned depth = 0;
    for (size_t m = n; m > 1; m >>= 1)
      depth += 2;
    ucpset_sort_(x, n, depth);
  }

  // Coalesce overlapping and adjacent intervals.
  size_t k = 0;
  for (size_t i = 0; i < n; i++) {
    uint32_t lo = x[2 * i];
    uint32_t hi = x[2 * i + 1];
    if (k > 0 && lo <= x[2 * k - 1]) {
      x[2 * k - 1] = ucpset_max(x[2 * k - 1], hi);
    } else {
      x[2 * k] = lo;
      x[2 * k + 1] = hi;
      k++;
    }
  }
  return k;
}

/*
 * Make x canonical from xcount codepoints stored in x[0, xcount): any order,
 * duplicates allowed.  x must have room for 2 * xcount words.  Values above
 * UCPSET_CODEPOINT_MAX are dropped.  Returns the new interval count.
 */
static inline size_t ucpset_normalize_codepoints(uint32_t* x, size_t xcount) {
  // Expand back to front into [v, v + 1) intervals; each write lands at or
  // after the codepoint it replaces, so no unread codepoint is overwritten.
  for (size_t i = xcount; i-- > 0;) {
    uint32_t v = x[i];
    x[2 * i] = v;
    x[2 * i + 1] = v + 1;
  }
  return ucpset_normalize(x, xcount);
}

#endif /* UCPSET_CANONICAL_H */
