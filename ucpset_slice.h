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
#ifndef UCPSET_SLICE_H
#define UCPSET_SLICE_H
/*
 * ucpset_slice.h — a run of intervals of a set, by position
 *
 * Any contiguous run of a canonical set is itself canonical, so taking one
 * is a single block move.  Pairs with the index ranges returned by
 * ucpset_find_overlap_interval and ucpset_find_coalesce_interval.
 *
 * To select by codepoint value instead, use ucpset_intersection_interval.
 */
#include <stddef.h>
#include <stdint.h>

#include "ucpset_internal.h"

/*
 * dst = intervals [i, j) of x.  Returns the count, j - i.
 *
 * Requires i <= j <= the interval count of x; dst needs room for j - i
 * intervals.  dst may overlap x.
 */
static inline size_t ucpset_slice(uint32_t* dst, const uint32_t* x,
                                  size_t i, size_t j) {
  return ucpset_move(dst, x + 2 * i, j - i);
}

#endif /* UCPSET_SLICE_H */
