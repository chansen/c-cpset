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
#ifndef UCPSET_CARDINALITY_H
#define UCPSET_CARDINALITY_H
/*
 * ucpset_cardinality.h — number of codepoints in a set
 *
 * ucpset_cardinality counts the codepoints of one set, and
 * ucpset_cardinality_intersection those two sets share, without building
 * the intersection.  The other operations follow from the two:
 *
 *   |x ∪ y| = |x| + |y| − |x ∩ y|
 *   |x − y| = |x| − |x ∩ y|
 *   |x △ y| = |x| + |y| − 2 |x ∩ y|
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ucpset_internal.h"
#include "ucpset_search.h"

// Number of codepoints in x.
static inline uint32_t ucpset_cardinality(const uint32_t* x, size_t xcount) {
  uint32_t total = 0;
  for (size_t i = 0; i < xcount; i++)
    total += (x[i * 2 + 1] - x[i * 2]);
  return total;
}

/*
 * Number of codepoints in both x and y.
 *
 * The loop is the intersection sweep of ucpset_sweep.h with the output
 * replaced by a sum: intervals that end before the other side's current
 * one are skipped, a long run of them at a gallop, and a long run of
 * intervals lying inside one wide interval of the other set is summed as a
 * block.
 */
static inline uint32_t ucpset_cardinality_intersection(const uint32_t* x, size_t xcount,
                                                       const uint32_t* y, size_t ycount) {
  if (xcount == 0 || ycount == 0)
    return 0;
  if (x[xcount * 2 - 1] <= y[0] || y[ycount * 2 - 1] <= x[0])
    return 0;

  const size_t xn = 2 * xcount, yn = 2 * ycount;
  size_t i = 0, j = 0;
  uint32_t card = 0;
  unsigned run_x = 0, run_y = 0;

  while (i < xn && j < yn) {
    const uint32_t x_lo = x[i], x_hi = x[i + 1];
    const uint32_t y_lo = y[j], y_hi = y[j + 1];

    if (x_hi <= y_lo) {
      run_y = 0;
      i = ucpset_skip_hi_(x, xn, i, y_lo, &run_x);
      continue;
    }
    if (y_hi <= x_lo) {
      run_x = 0;
      j = ucpset_skip_hi_(y, yn, j, x_lo, &run_y);
      continue;
    }

    card += ucpset_min(x_hi, y_hi) - ucpset_max(x_lo, y_lo);

    if (x_hi < y_hi) {
      // x ended inside y; the x intervals that also end within y count whole.
      i += 2;
      run_y = 0;
      if (++run_x >= UCPSET_GALLOP_MIN) {
        run_x = 0;
        size_t m = ucpset_gallop_upper_bound_hi(x + i, (xn - i) / 2, y_hi);
        card += ucpset_cardinality(x + i, m);
        i += 2 * m;
      }
    } else if (y_hi < x_hi) {
      j += 2;
      run_x = 0;
      if (++run_y >= UCPSET_GALLOP_MIN) {
        run_y = 0;
        size_t m = ucpset_gallop_upper_bound_hi(y + j, (yn - j) / 2, x_hi);
        card += ucpset_cardinality(y + j, m);
        j += 2 * m;
      }
    } else {
      i += 2;
      j += 2;
      run_x = 0;
      run_y = 0;
    }
  }

  return card;
}

#endif /* UCPSET_CARDINALITY_H */
