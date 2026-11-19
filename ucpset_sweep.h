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
#ifndef UCPSET_SWEEP_H
#define UCPSET_SWEEP_H
/*
 * ucpset_sweep.h — forward sweep cores, one loop per set operation
 *
 * Each core walks x and y left-to-right and writes the canonical result of
 * one boolean operation to dst.  The public operations in ucpset_merge.h
 * and ucpset_inplace.h run their fast paths first and then call these.
 *
 * The loops adapt to lopsided inputs.  Union, intersection and difference:
 * after UCPSET_GALLOP_MIN consecutive steps on one side they gallop over
 * the rest of the run (see ucpset_search.h), skipping it or copying it in
 * one block.  That covers both a run falling in a gap of the other set and
 * a run lying inside one wide interval of it.
 *
 * All cores accept empty inputs and return the number of output intervals.
 * dst needs capacity for (xcount + ycount) intervals and must not overlap
 * y.
 *
 * Each loop ends by copying what is left of one side.  That pointer is
 * formed once, after the loop: written as two copies (x + i, then y + j),
 * the compiler keeps both pointers up to date inside the loop, which costs
 * 10-20% on sets of similar size.
 *
 * x may sit in dst's own buffer, ycount intervals or more above its start:
 * the layout the in-place operations use.  That is safe because the output
 * never holds more than has been read from x and y together, and each piece
 * of x is read before anything is written for it, so a write always lands
 * below the unread part of x.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ucpset_internal.h"
#include "ucpset_search.h"

// Append one interval to the output.  *k is in array elements.
static inline void ucpset_sweep_emit_(uint32_t* dst, size_t* k,
                                      uint32_t lo, uint32_t hi) {
  dst[*k] = lo;
  dst[*k + 1] = hi;
  *k += 2;
}

// As ucpset_sweep_emit_, but the interval is kept only when `keep` is true.
// The slot is written either way, which avoids a branch on data; it is
// within capacity because it is where the interval would have gone.
static inline void ucpset_sweep_emit_if_(uint32_t* dst, size_t* k,
                                         uint32_t lo, uint32_t hi, bool keep) {
  dst[*k] = lo;
  dst[*k + 1] = hi;
  *k += 2 * (size_t)keep;
}

/*
 * Append the n array elements src[0, n) to the output unchanged: whole
 * intervals, or single boundaries for symmetric difference.
 * src may overlap the output from above (see the note on in-place use).
 */
static inline void ucpset_sweep_copy_(uint32_t* dst, size_t* k,
                                      const uint32_t* src, size_t n) {
  if (n)
    memmove(dst + *k, src, n * sizeof(uint32_t));
  *k += n;
}

/*
 * Union.  An interval that ends before the other side's next one starts,
 * with a gap, passes through unchanged: a run of those is copied one at a
 * time up to UCPSET_GALLOP_MIN, then in one block.  Otherwise the two
 * current intervals overlap or touch; they start a merged interval, which
 * absorbs every interval from either side that reaches it.
 *
 * When one set is much larger than the other, a merged interval may cover
 * a long run of the larger set's intervals, so the absorb loop gallops past
 * what it already covers.  For sets of comparable size it stays a plain
 * loop: counting there costs more than it saves.
 */
static UCPSET_NOINLINE size_t ucpset_sweep_union(uint32_t* dst,
                                        const uint32_t* x, size_t xcount,
                                        const uint32_t* y, size_t ycount) {
  const size_t xn = 2 * xcount, yn = 2 * ycount;
  const bool lopsided = ucpset_much_larger_(xcount, ycount) ||
                        ucpset_much_larger_(ycount, xcount);
  size_t i = 0, j = 0, k = 0;

  while (i < xn && j < yn) {
    if (x[i + 1] < y[j]) {
      // A run of x before y[j].  key is at least 1 here.
      const uint32_t key = y[j];
      unsigned n = 0;
      do {
        ucpset_sweep_emit_(dst, &k, x[i], x[i + 1]);
        i += 2;
      } while (++n < UCPSET_GALLOP_MIN && i < xn && x[i + 1] < key);
      if (n == UCPSET_GALLOP_MIN && i < xn && x[i + 1] < key) {
        size_t m = 2 * ucpset_gallop_lower_bound_hi(x + i, (xn - i) / 2, key);
        ucpset_sweep_copy_(dst, &k, x + i, m);
        i += m;
      }
      continue;
    }
    if (y[j + 1] < x[i]) {
      // A run of y before x[i].
      const uint32_t key = x[i];
      unsigned n = 0;
      do {
        ucpset_sweep_emit_(dst, &k, y[j], y[j + 1]);
        j += 2;
      } while (++n < UCPSET_GALLOP_MIN && j < yn && y[j + 1] < key);
      if (n == UCPSET_GALLOP_MIN && j < yn && y[j + 1] < key) {
        size_t m = 2 * ucpset_gallop_lower_bound_hi(y + j, (yn - j) / 2, key);
        ucpset_sweep_copy_(dst, &k, y + j, m);
        j += m;
      }
      continue;
    }

    // Overlapping or touching: merge, then absorb whatever reaches hi.
    const uint32_t lo = ucpset_min(x[i], y[j]);
    uint32_t hi = ucpset_max(x[i + 1], y[j + 1]);
    i += 2;
    j += 2;

    if (!lopsided) {
      for (;;) {
        if (i < xn && x[i] <= hi) {
          hi = ucpset_max(hi, x[i + 1]);
          i += 2;
        } else if (j < yn && y[j] <= hi) {
          hi = ucpset_max(hi, y[j + 1]);
          j += 2;
        } else {
          break;
        }
      }
    } else {
      unsigned n = 0;
      for (;;) {
        if (i < xn && x[i] <= hi) {
          hi = ucpset_max(hi, x[i + 1]);
          i += 2;
        } else if (j < yn && y[j] <= hi) {
          hi = ucpset_max(hi, y[j + 1]);
          j += 2;
        } else {
          break;
        }
        // A long absorb: gallop past the intervals hi already covers.
        if (++n == UCPSET_GALLOP_MIN) {
          n = 0;
          i += 2 * ucpset_gallop_lower_bound_hi(x + i, (xn - i) / 2, hi);
          j += 2 * ucpset_gallop_lower_bound_hi(y + j, (yn - j) / 2, hi);
        }
      }
    }

    ucpset_sweep_emit_(dst, &k, lo, hi);
  }

  // One side is exhausted; the rest of the other lies strictly beyond.
  const uint32_t* rest = (i < xn) ? x + i : y + j;
  const size_t n = (i < xn) ? xn - i : yn - j;
  ucpset_sweep_copy_(dst, &k, rest, n);

  return k / 2;
}

/*
 * Intersection: skip intervals that end before the other side's current
 * one starts; otherwise emit the overlap and advance whichever interval
 * ends first (both when they end together).
 *
 * run_x and run_y count how many times in a row only that side advanced.
 * A long run of skips is galloped over.  A long run of x intervals ending
 * inside one y interval lies wholly within it, and is copied in one block.
 */
static UCPSET_NOINLINE size_t ucpset_sweep_intersection(uint32_t* dst,
                                               const uint32_t* x, size_t xcount,
                                               const uint32_t* y, size_t ycount) {
  const size_t xn = 2 * xcount, yn = 2 * ycount;
  size_t i = 0, j = 0, k = 0;
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

    ucpset_sweep_emit_(dst, &k, ucpset_max(x_lo, y_lo),
                       ucpset_min(x_hi, y_hi));

    if (x_hi < y_hi) {
      // x ended inside y; the x intervals that also end within y are part
      // of the result as they are.
      i += 2;
      run_y = 0;
      if (++run_x >= UCPSET_GALLOP_MIN) {
        run_x = 0;
        size_t m = 2 * ucpset_gallop_upper_bound_hi(x + i, (xn - i) / 2, y_hi);
        ucpset_sweep_copy_(dst, &k, x + i, m);
        i += m;
      }
    } else if (y_hi < x_hi) {
      j += 2;
      run_x = 0;
      if (++run_y >= UCPSET_GALLOP_MIN) {
        run_y = 0;
        size_t m = 2 * ucpset_gallop_upper_bound_hi(y + j, (yn - j) / 2, x_hi);
        ucpset_sweep_copy_(dst, &k, y + j, m);
        j += m;
      }
    } else {
      i += 2;
      j += 2;
      run_x = 0;
      run_y = 0;
    }
  }

  return k / 2;
}

/*
 * Difference (x − y): for each x interval, skip the y intervals before it.
 * If none of the rest touches it, it passes through unchanged.  Otherwise
 * emit the pieces left between the y intervals that overlap it.  A y
 * interval reaching past the end of the current x interval is kept for the
 * next one.
 *
 * run_x counts x intervals handled in a row against the same y interval.
 * A long run of untouched ones is copied in one block; a long run of ones
 * that y covers to the end is skipped at once.
 */
static UCPSET_NOINLINE size_t ucpset_sweep_difference(uint32_t* dst,
                                             const uint32_t* x, size_t xcount,
                                             const uint32_t* y, size_t ycount) {
  const size_t xn = 2 * xcount, yn = 2 * ycount;
  size_t i = 0, j = 0, k = 0;
  unsigned run_x = 0;

  while (i < xn && j < yn) {
    uint32_t lo = x[i];
    const uint32_t hi = x[i + 1];
    i += 2;

    // Skip y intervals entirely before this x interval.
    unsigned run_y = 0;
    while (j < yn && y[j + 1] <= lo)
      j = ucpset_skip_hi_(y, yn, j, lo, &run_y);

    if (j == yn || hi <= y[j]) {
      // Untouched by y: passes through unchanged.
      ucpset_sweep_emit_(dst, &k, lo, hi);
      if (j < yn && ++run_x >= UCPSET_GALLOP_MIN) {
        run_x = 0;
        size_t m = 2 * ucpset_gallop_upper_bound_hi(x + i, (xn - i) / 2, y[j]);
        ucpset_sweep_copy_(dst, &k, x + i, m);
        i += m;
      }
      continue;
    }

    // y[j] ends after lo and starts before hi; carve out it and each
    // following y interval that starts before hi.
    for (;;) {
      const uint32_t y_lo = y[j];
      const uint32_t y_hi = y[j + 1];

      // Whether a piece is left in front of y[j] depends on the data, so
      // it is emitted without branching on it.
      ucpset_sweep_emit_if_(dst, &k, lo, y_lo, lo < y_lo);

      if (y_hi >= hi) {
        // y[j] covers the rest and is kept for the next x interval.  The
        // x intervals that also end within it are covered whole.
        if (++run_x >= UCPSET_GALLOP_MIN) {
          run_x = 0;
          i += 2 * ucpset_gallop_upper_bound_hi(x + i, (xn - i) / 2, y_hi);
        }
        break;
      }

      lo = y_hi;
      j += 2;
      if (!(j < yn && y[j] < hi)) {
        // Nothing more of y inside this x interval: the rest survives.
        ucpset_sweep_emit_(dst, &k, lo, hi);
        run_x = 0;
        break;
      }
    }
  }

  // y is exhausted; the rest of x passes through unchanged.
  ucpset_sweep_copy_(dst, &k, x + i, xn - i);

  return k / 2;
}

// First index t in [0, n) with a[t] >= key, or n, for a strictly increasing
// array of boundaries.  Exponential search from the front, then a binary
// search of the bracket: O(log t).
static inline size_t ucpset_gallop_boundary_(const uint32_t* a, size_t n,
                                             uint32_t key) {
  size_t base = 0;
  size_t step = 1;
  while (base + step <= n && a[base + step - 1] < key) {
    base += step;
    step *= 2;
  }

  const uint32_t* p = a + base;
  size_t len = (base + step <= n) ? step - 1 : n - base;
  while (len > 0) {
    size_t half = len / 2;
    if (p[half] < key) {
      p += half + 1;
      len -= half + 1;
    } else {
      len = half;
    }
  }
  return (size_t)(p - a);
}

/*
 * Symmetric difference when one set is much larger than the other: long
 * runs of the larger set's boundaries fall between two boundaries of the
 * smaller.  A run is copied one boundary at a time up to UCPSET_GALLOP_MIN
 * intervals' worth, then in one block.
 */
static UCPSET_NOINLINE size_t ucpset_sweep_xor_runs_(uint32_t* dst,
                                            const uint32_t* x, size_t xcount,
                                            const uint32_t* y, size_t ycount) {
  const size_t xn = 2 * xcount, yn = 2 * ycount;
  const unsigned run_max = 2 * UCPSET_GALLOP_MIN;
  size_t i = 0, j = 0, k = 0;

  while (i < xn && j < yn) {
    const uint32_t a = x[i];
    const uint32_t b = y[j];
    if (a < b) {
      // A run of x boundaries below b.
      unsigned n = 0;
      do {
        dst[k++] = x[i];
        i++;
      } while (++n < run_max && i < xn && x[i] < b);
      if (n == run_max && i < xn && x[i] < b) {
        size_t m = ucpset_gallop_boundary_(x + i, xn - i, b);
        ucpset_sweep_copy_(dst, &k, x + i, m);
        i += m;
      }
    } else if (b < a) {
      // A run of y boundaries below a.
      unsigned n = 0;
      do {
        dst[k++] = y[j];
        j++;
      } while (++n < run_max && j < yn && y[j] < a);
      if (n == run_max && j < yn && y[j] < a) {
        size_t m = ucpset_gallop_boundary_(y + j, yn - j, a);
        ucpset_sweep_copy_(dst, &k, y + j, m);
        j += m;
      }
    } else {
      i++;
      j++;
    }
  }

  // One side is exhausted; the rest of the other passes through unchanged.
  const uint32_t* rest = (i < xn) ? x + i : y + j;
  const size_t n = (i < xn) ? xn - i : yn - j;
  ucpset_sweep_copy_(dst, &k, rest, n);

  return k / 2;
}

/*
 * Symmetric difference: the flat arrays are strictly increasing boundary
 * lists, and x ^ y is their sorted merge with boundaries present in both
 * cancelled.  For sets of comparable size this is a plain merge; lopsided
 * ones go to ucpset_sweep_xor_runs_.
 */
static UCPSET_NOINLINE size_t ucpset_sweep_xor(uint32_t* dst,
                                      const uint32_t* x, size_t xcount,
                                      const uint32_t* y, size_t ycount) {
  if (ucpset_much_larger_(xcount, ycount) ||
      ucpset_much_larger_(ycount, xcount))
    return ucpset_sweep_xor_runs_(dst, x, xcount, y, ycount);

  const size_t xn = 2 * xcount, yn = 2 * ycount;
  size_t i = 0, j = 0, k = 0;

  while (i < xn && j < yn) {
    uint32_t a = x[i];
    uint32_t b = y[j];
    if (a == b) {
      i++;
      j++;
      continue;
    }
    uint32_t v;
    if (a < b) {
      v = a;
      i++;
    } else {
      v = b;
      j++;
    }
    dst[k] = v;
    k++;
  }

  // One side is exhausted; the rest of the other passes through unchanged.
  const uint32_t* rest = (i < xn) ? x + i : y + j;
  const size_t n = (i < xn) ? xn - i : yn - j;
  ucpset_sweep_copy_(dst, &k, rest, n);

  return k / 2;
}

#endif /* UCPSET_SWEEP_H */
