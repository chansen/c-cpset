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
#ifndef UCPSET_SPLICE_H
#define UCPSET_SPLICE_H
/*
 * ucpset_splice.h — replace a run of intervals in place
 *
 * The building block for edits by position: locate the affected run with
 * ucpset_find_overlap_interval or ucpset_find_coalesce_interval, work out
 * the intervals that replace it, then splice them in.
 */
#include <stddef.h>
#include <stdint.h>

#include "ucpset_internal.h"

/*
 * Replace intervals [i, j) of x with the ycount intervals of y.
 * Returns the new interval count: xcount - (j - i) + ycount.
 *
 * Requires i <= j <= xcount; x needs room for the new count.  y must not
 * overlap x's buffer.
 *
 * The result is not checked.  The caller guarantees it is canonical: y is
 * canonical and fits, with a gap on each side, between interval i - 1 and
 * interval j of x.
 */
static inline size_t ucpset_splice(uint32_t* x, size_t xcount,
                                   size_t i, size_t j,
                                   const uint32_t* y, size_t ycount) {
  size_t n = ucpset_open_run(x, xcount, i, j, ycount);
  ucpset_copy(x + 2 * i, y, ycount);
  return n;
}

#endif /* UCPSET_SPLICE_H */
