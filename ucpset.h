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
#ifndef UCPSET_H
#define UCPSET_H
/*
 * ucpset.h — Unicode codepoint interval set library (header-only)
 *
 * Efficient set operations over Unicode codepoints (U+0000..U+10FFFF),
 * represented as sorted, disjoint, non-adjacent half-open intervals [lo, hi)
 * stored in a flat uint32_t array.
 *
 * Strict canonical invariant:
 *   - Array length is even (pairs of lo, hi).
 *   - Each pair satisfies lo < hi.
 *   - Intervals are sorted, disjoint, and non-adjacent: prev_hi < next_lo.
 *
 * All functions assume inputs are strictly canonical.
 * All functions are static inline (header-only).
 *
 * Usage:
 *   #include "ucpset.h"
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ucprange_set.h"
#include "ucpset_canonical.h"
#include "ucpset_cardinality.h"
#include "ucpset_inplace.h"
#include "ucpset_insert.h"
#include "ucpset_internal.h"
#include "ucpset_membership.h"
#include "ucpset_merge.h"
#include "ucpset_predicate.h"
#include "ucpset_remove.h"
#include "ucpset_search.h"
#include "ucpset_slice.h"
#include "ucpset_splice.h"
#include "ucpset_sweep.h"

#endif /* UCPSET_H */
