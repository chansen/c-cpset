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
#ifndef UCPRANGE_SET_H
#define UCPRANGE_SET_H
/*
 * ucprange_set.h — ucprange_t wrappers for ucpset operations
 *
 * Thin wrappers that accept ucprange_t instead of raw lo/hi pairs,
 * bridging the typed range API with the interval-based set operations.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ucprange.h"
#include "ucpset_insert.h"
#include "ucpset_remove.h"
#include "ucpset_membership.h"
#include "ucpset_merge.h"
#include "ucpset_search.h"

static inline size_t ucpset_insert_range(uint32_t* x, size_t xcount,
                                         ucprange_t range) {
  return ucpset_insert_interval(x, xcount, range.lo, range.hi);
}

static inline size_t ucpset_remove_range(uint32_t* x, size_t xcount,
                                         ucprange_t range) {
  return ucpset_remove_interval(x, xcount, range.lo, range.hi);
}

static inline bool ucpset_contains_range(const uint32_t* x,
                                         size_t xcount,
                                         ucprange_t range) {
  return ucpset_contains_interval(x, xcount, range.lo, range.hi);
}

static inline void ucpset_find_overlap_range(const uint32_t* x, size_t xcount,
                                             ucprange_t range,
                                             size_t* i, size_t* j) {
  ucpset_find_overlap_interval(x, xcount, range.lo, range.hi, i, j);
}

static inline void ucpset_find_coalesce_range(const uint32_t* x, size_t xcount,
                                              ucprange_t range,
                                              size_t* i, size_t* j) {
  ucpset_find_coalesce_interval(x, xcount, range.lo, range.hi, i, j);
}

static inline size_t ucpset_union_range(uint32_t* dst, const uint32_t* x,
                                        size_t xcount, ucprange_t range) {
  return ucpset_union_interval(dst, x, xcount, range.lo, range.hi);
}

static inline size_t ucpset_intersection_range(uint32_t* dst, const uint32_t* x,
                                               size_t xcount, ucprange_t range) {
  return ucpset_intersection_interval(dst, x, xcount, range.lo, range.hi);
}

static inline size_t ucpset_difference_range(uint32_t* dst, const uint32_t* x,
                                             size_t xcount, ucprange_t range) {
  return ucpset_difference_interval(dst, x, xcount, range.lo, range.hi);
}

static inline size_t ucpset_xor_range(uint32_t* dst, const uint32_t* x,
                                      size_t xcount, ucprange_t range) {
  return ucpset_xor_interval(dst, x, xcount, range.lo, range.hi);
}

#endif /* UCPRANGE_SET_H */
