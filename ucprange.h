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
#ifndef UCPRANGE_H
#define UCPRANGE_H
/*
 * ucprange.h — half-open Unicode range [lo, hi)
 *
 * A typed value representing a contiguous range of codepoints.
 * Used as the user-facing API type; the internal flat uint32_t array
 * representation uses untyped interval pairs.
 */
#include <stdbool.h>
#include <stdint.h>

#define UCPRANGE_CODEPOINT_MAX  0x10FFFF   // maximum valid Unicode code point
#define UCPRANGE_BOUND_MAX      0x110000   // exclusive upper bound

typedef struct {
  uint32_t lo;
  uint32_t hi;
} ucprange_t;

static inline ucprange_t ucprange(uint32_t lo, uint32_t hi) {
  return (ucprange_t){lo, hi};
}

static inline uint32_t ucprange_cardinality(ucprange_t r) {
  return r.lo < r.hi ? r.hi - r.lo : 0;
}

static inline bool ucprange_is_empty(ucprange_t r) {
  return r.lo >= r.hi;
}

static inline bool ucprange_is_singleton(ucprange_t r) {
  return r.lo + 1 == r.hi;
}

// valid Unicode range: non-empty and within [0, 0x110000).
static inline bool ucprange_is_valid(ucprange_t r) {
  return r.lo < r.hi && r.hi <= UCPRANGE_BOUND_MAX;
}

static inline bool ucprange_equals(ucprange_t x, ucprange_t y) {
  return x.lo == y.lo && x.hi == y.hi;
}

static inline bool ucprange_contains(ucprange_t r, uint32_t value) {
  return value >= r.lo && value < r.hi;
}

// x is entirely contained within y.
static inline bool ucprange_subset(ucprange_t x, ucprange_t y) {
  return x.lo >= y.lo && x.hi <= y.hi;
}

// x entirely contains y.
static inline bool ucprange_superset(ucprange_t x, ucprange_t y) {
  return ucprange_subset(y, x);
}

// x and y share no codepoints.
static inline bool ucprange_disjoint(ucprange_t x, ucprange_t y) {
  return x.hi <= y.lo || y.hi <= x.lo;
}

// x and y are adjacent (disjoint but would would merge into one interval).
static inline bool ucprange_adjacent(ucprange_t x, ucprange_t y) {
  return x.hi == y.lo || y.hi == x.lo;
}

// x and y overlap or are adjacent (would merge into one interval).
static inline bool ucprange_coalesce(ucprange_t x, ucprange_t y) {
  return x.hi >= y.lo && y.hi >= x.lo;
}

// x lies entirely before y.
static inline bool ucprange_precedes(ucprange_t x, ucprange_t y) {
  return x.hi <= y.lo;
}

// x lies entirely after y.
static inline bool ucprange_succeeds(ucprange_t x, ucprange_t y) {
  return x.lo >= y.hi;
}

// Lexicographic ordering: by lo, then by hi.
static inline int ucprange_compare(ucprange_t x, ucprange_t y) {
  if (x.lo != y.lo)
    return (x.lo < y.lo) ? -1 : 1;
  if (x.hi != y.hi)
    return (x.hi < y.hi) ? -1 : 1;
  return 0;
}

/*
 * Classify the spatial relationship between two ranges.
 *
 * Returns:
 *  -4  x precedes y with gap (disjoint, x.hi < y.lo)
 *  -3  x is adjacent to y from the left (x.hi == y.lo, coalesces)
 *  -2  x overlaps y from the left (x.lo < y.lo, x.hi < y.hi)
 *  -1  x is a proper superset of y
 *   0  x equals y
 *   1  x is a proper subset of y
 *   2  x overlaps y from the right (x.lo > y.lo, x.hi > y.hi)
 *   3  x is adjacent to y from the right (x.lo == y.hi, coalesces)
 *   4  x succeeds y with gap (disjoint, x.lo > y.hi)
 */
static inline int ucprange_relation(ucprange_t x, ucprange_t y) {
  if (x.lo < y.lo) {
    if (x.hi < y.lo)                   // gap, x before y
      return -4;
    if (x.hi == y.lo)                  // adjacent, x before y
      return -3;
    return (x.hi >= y.hi) ? -1 : -2;   // superset or left overlap
  }
  if (y.lo < x.lo) {
    if (y.hi < x.lo)                   // gap, y before x
      return 4;
    if (y.hi == x.lo)                  // adjacent, y before x
      return 3;
    return (y.hi >= x.hi) ? 1 : 2;     // subset or right overlap
  }
  // x.lo == y.lo: compare hi values
  return (x.hi < y.hi) ? 1 : (x.hi > y.hi) ? -1 : 0;
}

// Shift range right by amount: [lo + amount, hi + amount).
static inline ucprange_t ucprange_plus(ucprange_t r, uint32_t amount) {
  return (ucprange_t){r.lo + amount, r.hi + amount};
}

// Shift range left by amount: [lo - amount, hi - amount).
static inline ucprange_t ucprange_minus(ucprange_t r, uint32_t amount) {
  return (ucprange_t){r.lo - amount, r.hi - amount};
}

// Part of x before y: [x.lo, min(x.hi, y.lo)). May be empty.
static inline ucprange_t ucprange_prefix(ucprange_t x, ucprange_t y) {
  uint32_t hi = (x.hi < y.lo) ? x.hi : y.lo;
  return (ucprange_t){x.lo, hi};
}

// Part of x after y: [max(x.lo, y.hi), x.hi). May be empty.
static inline ucprange_t ucprange_suffix(ucprange_t x, ucprange_t y) {
  uint32_t lo = (x.lo > y.hi) ? x.lo : y.hi;
  return (ucprange_t){lo, x.hi};
}

// Intersection of x and y: [max(lo), min(hi)). May be empty.
static inline ucprange_t ucprange_intersection(ucprange_t x, ucprange_t y) {
  uint32_t lo = (x.lo > y.lo) ? x.lo : y.lo;
  uint32_t hi = (x.hi < y.hi) ? x.hi : y.hi;
  return (ucprange_t){lo, hi};
}

// Smallest range covering both x and y: [min(lo), max(hi)).
static inline ucprange_t ucprange_span(ucprange_t x, ucprange_t y) {
  uint32_t lo = (x.lo < y.lo) ? x.lo : y.lo;
  uint32_t hi = (x.hi > y.hi) ? x.hi : y.hi;
  return (ucprange_t){lo, hi};
}

#endif /* UCPRANGE_H */
