# c-ucpset

A header-only C library for sets of Unicode codepoints, stored as sorted half-open intervals in a flat `uint32_t` array.

A set is a plain array owned by the caller. There is no set type, no heap allocation, and no global state: every function reads and writes caller-supplied buffers. Because a set is just an array, it can also be `static const` data, such as a table generated from the Unicode Character Database.

The library provides membership tests, insertion and removal, the boolean operations (union, intersection, difference, symmetric difference, complement) in out-of-place and in-place forms, operations on a single interval, and the usual set predicates.

## Usage

```c
#include "ucpset.h"

// Build [0-9A-Fa-f]. Insertion needs room for one more interval.
uint32_t hex[2 * 4];
size_t   hex_count = 0;

hex_count = ucpset_insert_interval(hex, hex_count, '0', '9' + 1);
hex_count = ucpset_insert_interval(hex, hex_count, 'A', 'F' + 1);
hex_count = ucpset_insert_interval(hex, hex_count, 'a', 'f' + 1);

bool is_hex = ucpset_contains(hex, hex_count, 'c');   // true

// A set can be constant data: two intervals, [a-z] and [α-ω].
static const uint32_t lower[] = { 0x0061, 0x007B, 0x03B1, 0x03CA };

// Out-of-place operations write to a separate buffer
// with room for (xcount + ycount) intervals.
uint32_t dst[2 * (3 + 2)];
size_t   count = ucpset_intersection(dst, hex, hex_count, lower, 2);
// dst = { 0x61, 0x67 }, count = 1: [a-f]
```

All counts are in intervals, not array elements: a set of `n` intervals occupies `2 * n` elements.

## Requirements

- C99 or later

## Representation

A set of `n` intervals is an array of `2 * n` values `lo0, hi0, lo1, hi1, ...`. Each pair is a half-open interval `[lo, hi)`: `lo` is the first codepoint in the interval and `hi` is one past the last.

```c
// { U+0041..U+005A, U+0061..U+007A }
uint32_t letters[] = { 0x41, 0x5B, 0x61, 0x7B };
```

Every set must be in strict canonical form:

- each interval is non-empty: `lo < hi`
- intervals are sorted, disjoint, and non-adjacent: the `hi` of one interval is strictly less than the `lo` of the next
- every `hi` is at most `0x110000`, one past the last Unicode codepoint

Canonical form makes the representation unique, so two equal sets are equal arrays, and the array as a whole is a strictly increasing list of boundaries (an inversion list).

All functions assume canonical input and produce canonical output. Passing a non-canonical array is undefined behaviour; `ucpset_canonical()` checks an array that comes from outside, and `ucpset_normalize()` makes one canonical.

### Buffer capacity

No function allocates. The caller provides the output space, and the worst case is known up front:

| Operation                                        | Required capacity (intervals)      |
|:-------------------------------------------------|:-----------------------------------|
| `ucpset_insert`, `ucpset_insert_interval`        | `xcount + 1` in `x`                |
| `ucpset_remove`, `ucpset_remove_interval`        | `xcount + 1` in `x`                |
| `ucpset_union`, `ucpset_intersection`, `ucpset_difference`, `ucpset_xor` | `xcount + ycount` in `dst` |
| `ucpset_complement`                              | `xcount + 1` in `dst`              |
| `ucpset_union_interval`, `ucpset_difference_interval`, `ucpset_xor_interval` | `xcount + 1` in `dst` |
| `ucpset_intersection_interval`                   | `xcount` in `dst`                  |
| `ucpset_inplace_union`, `_intersection`, `_difference`, `_xor` | `xcount + ycount` in `x` |
| `ucpset_inplace_complement`                      | `xcount + 1` in `x`                |
| `ucpset_splice`                                  | `xcount - (j - i) + ycount` in `x` |
| `ucpset_normalize`                               | `xcount` in `x`: no extra room     |
| `ucpset_normalize_codepoints`                    | `xcount` in `x`: one interval per codepoint |
| `ucpset_slice`                                   | `j - i` in `dst`                   |

These are upper bounds. To end up with a buffer of exactly the right size, allocate the bound, run the operation, and shrink to the count it returns. That is cheaper than counting first, which would cost as much as the operation itself.

### Out-of-range and empty arguments

The Unicode range is enforced at the points where values enter a set. `ucpset_insert` ignores a value above U+10FFFF. `ucpset_insert_interval`, `ucpset_union_interval`, `ucpset_xor_interval`, and the complement family clamp `hi` to `0x110000`, and an interval that lies entirely above it is ignored.

An interval with `lo >= hi` is empty. Inserting or removing it does nothing, a complement over it is empty, and `ucpset_contains_interval` returns `true` for it, since the empty set is a subset of every set.

## API

The library is split into independent headers. `ucpset.h` includes all of them.

---

### Canonical form — `ucpset_canonical.h`

```c
bool   ucpset_canonical(const uint32_t *x, size_t xcount);
size_t ucpset_normalize(uint32_t *x, size_t xcount);
size_t ucpset_normalize_codepoints(uint32_t *x, size_t xcount);
```

**`ucpset_canonical`** returns `true` if `x` satisfies the canonical invariant described under [Representation](#representation). The empty set is canonical.

**`ucpset_normalize`** makes an arbitrary list of `xcount` intervals canonical, in place, and returns the new interval count. The intervals may be in any order and may overlap or touch; empty and reversed ones (`lo >= hi`) are dropped, and `hi` is clamped to `0x110000`. It takes O(n) when the list is already sorted by `lo` and O(n log n) otherwise, using an inline sort with no callbacks and no allocation.

**`ucpset_normalize_codepoints`** does the same for a list of `xcount` single codepoints stored in `x[0, xcount)`, in any order and with duplicates allowed. Each codepoint becomes an interval, so `x` needs room for `2 * xcount` elements. Values above U+10FFFF are dropped.

This is the way to build a set from data that does not arrive in order, such as ranges parsed from a Unicode data file:

```c
uint32_t buf[2 * MAX_RANGES];
size_t   n = 0;

// for each "XXXX..YYYY ; Property" line, in file order:
buf[2 * n]     = first;
buf[2 * n + 1] = last + 1;
n++;

n = ucpset_normalize(buf, n);
```

Inserting the intervals one at a time gives the same set, but each insertion out of order moves the tail of the array, so it is quadratic in the worst case.

---

### Membership — `ucpset_membership.h`

```c
bool ucpset_contains(const uint32_t *x, size_t xcount, uint32_t v);
bool ucpset_contains_hint(const uint32_t *x, size_t xcount, uint32_t v,
                          size_t *hint);
bool ucpset_contains_interval(const uint32_t *x, size_t xcount,
                              uint32_t lo, uint32_t hi);
```

**`ucpset_contains`** returns `true` if codepoint `v` is in the set. A value below the end of the first interval, or at or past the end of the last, is decided in constant time; anything else takes a binary search over the intervals.

**`ucpset_contains_hint`** gives the same answer as `ucpset_contains`, and is meant for codepoints taken from text in order. Consecutive characters usually belong to the same script, so the next lookup tends to land in or next to the interval the last one found. `*hint` remembers that position: the lookup starts there and searches outward, which costs a few comparisons when the codepoint is nearby and grows with the logarithm of the distance when it is not.

```c
size_t hint = 0;
size_t letters = 0;

for (size_t k = 0; k < len; k++)
  letters += ucpset_contains_hint(set, set_count, text[k], &hint);
```

Set the hint to 0 before the first lookup and keep one hint per set and per stream of text. Any value is accepted: a hint that is off, or left over from before the set was edited, costs time but never gives a wrong answer. A codepoint below the end of the first interval is answered without the hint and leaves it unchanged, so spaces and ASCII punctuation between words do not move it. For codepoints in arbitrary order, use `ucpset_contains`; the hint then misses every time and the lookup is slower.

**`ucpset_contains_interval`** returns `true` if every codepoint in `[lo, hi)` is in the set. It gives the same answer as `ucpset_subset` for the one-interval set `{[lo, hi)}`, and returns `true` when `lo >= hi`.

---

### Insertion — `ucpset_insert.h`

```c
size_t ucpset_insert(uint32_t *x, size_t xcount, uint32_t v);
size_t ucpset_insert_interval(uint32_t *x, size_t xcount,
                              uint32_t lo, uint32_t hi);
```

**`ucpset_insert`** adds codepoint `v` to the set in place and returns the new interval count. The count grows by one when `v` starts a new interval, stays the same when `v` extends an existing interval or is already present, and shrinks by one when `v` joins two intervals.

**`ucpset_insert_interval`** adds `[lo, hi)`, merging every interval it overlaps or touches, and returns the new interval count.

Both append in constant time when the new value lies at or beyond the last interval, so building a set in ascending order is linear. Otherwise the cost is a binary search plus one `memmove`.

---

### Removal — `ucpset_remove.h`

```c
size_t ucpset_remove(uint32_t *x, size_t xcount, uint32_t v);
size_t ucpset_remove_interval(uint32_t *x, size_t xcount,
                              uint32_t lo, uint32_t hi);
```

**`ucpset_remove`** removes codepoint `v` in place and returns the new interval count. Removing a value from the middle of an interval splits it in two, which is why `x` needs room for one more interval.

**`ucpset_remove_interval`** removes `[lo, hi)`, trimming or dropping every interval it overlaps, and returns the new interval count. The cost is two binary searches plus one `memmove`.

---

### Editing by position — `ucpset_splice.h`, `ucpset_slice.h`

```c
size_t ucpset_splice(uint32_t *x, size_t xcount, size_t i, size_t j,
                     const uint32_t *y, size_t ycount);
size_t ucpset_slice(uint32_t *dst, const uint32_t *x, size_t i, size_t j);
```

These work on runs of intervals by index instead of by codepoint value. They pair with `ucpset_find_overlap_interval` and `ucpset_find_coalesce_interval`, which return the run `[i, j)` that an interval affects (see [Search](#search--ucpset_searchh)).

**`ucpset_splice`** replaces intervals `[i, j)` of `x` with the `ycount` intervals of `y`, in place, and returns the new interval count. `y` must not overlap `x`. The result is not checked: the caller guarantees that `y` is canonical and fits between interval `i - 1` and interval `j` with a gap on each side.

**`ucpset_slice`** copies intervals `[i, j)` of `x` to `dst` and returns `j - i`. Any contiguous run of a canonical set is itself canonical. `dst` may overlap `x`. To select by codepoint value instead, use `ucpset_intersection_interval`.

A custom edit is a lookup followed by a splice. This is `ucpset_insert_interval` written out:

```c
size_t i, j;
ucpset_find_coalesce_interval(x, xcount, lo, hi, &i, &j);

uint32_t merged[2] = { lo, hi };
if (i < j) {
  if (x[2 * i] < lo)     merged[0] = x[2 * i];
  if (x[2 * j - 1] > hi) merged[1] = x[2 * j - 1];
}
xcount = ucpset_splice(x, xcount, i, j, merged, 1);
```

#### Block helpers — `ucpset_internal.h`

```c
size_t ucpset_copy(uint32_t *dst, const uint32_t *src, size_t n);
size_t ucpset_move(uint32_t *dst, const uint32_t *src, size_t n);
size_t ucpset_open_run(uint32_t *x, size_t xcount,
                       size_t i, size_t j, size_t k);
size_t ucpset_open_run_to(uint32_t *dst, const uint32_t *x, size_t xcount,
                          size_t i, size_t j, size_t k);
```

The library's own edits are built from these four, and they are available for edits it does not provide. All counts are in intervals.

**`ucpset_copy`** copies `n` intervals from `src` to `dst`, which must not overlap. **`ucpset_move`** does the same when they may overlap. Both return `n` and do nothing when `n` is 0.

**`ucpset_open_run`** replaces intervals `[i, j)` of `x` with `k` unwritten slots, in place, by moving the intervals after `j`; the caller then fills in the slots at `x + 2 * i`. It returns the new interval count, `xcount - (j - i) + k`. `ucpset_splice` is this followed by a copy.

**`ucpset_open_run_to`** does the same into `dst`, copying the intervals before `i` as well. `dst` is `x` itself or does not overlap it.

---

### Set operations — `ucpset_merge.h`

```c
size_t ucpset_union(uint32_t *dst,
                    const uint32_t *x, size_t xcount,
                    const uint32_t *y, size_t ycount);
size_t ucpset_intersection(uint32_t *dst,
                           const uint32_t *x, size_t xcount,
                           const uint32_t *y, size_t ycount);
size_t ucpset_difference(uint32_t *dst,
                         const uint32_t *x, size_t xcount,
                         const uint32_t *y, size_t ycount);
size_t ucpset_xor(uint32_t *dst,
                  const uint32_t *x, size_t xcount,
                  const uint32_t *y, size_t ycount);
size_t ucpset_complement(uint32_t *dst,
                         const uint32_t *x, size_t xcount,
                         uint32_t lo, uint32_t hi);
```

Each function writes the result to `dst` and returns its interval count. `dst` must not overlap `x` or `y`.

**`ucpset_union`**, **`ucpset_intersection`**, **`ucpset_difference`** (`x − y`), and **`ucpset_xor`** (symmetric difference) each run a single forward pass over both inputs. Empty inputs, and inputs where one set lies entirely before the other, take a shortcut instead. When either set is a single interval, the [one-interval](#one-interval) operations are used.

**`ucpset_complement`** writes the codepoints of the universe `[lo, hi)` that are not in `x`. Pass `0` and `0x110000` for the complement over all of Unicode. The gaps between the intervals of `x` are already in the array, so this is two searches and one block copy. `dst` may be `x` itself.

#### One interval

```c
size_t ucpset_union_interval(uint32_t *dst,
                             const uint32_t *x, size_t xcount,
                             uint32_t lo, uint32_t hi);
size_t ucpset_intersection_interval(uint32_t *dst,
                                    const uint32_t *x, size_t xcount,
                                    uint32_t lo, uint32_t hi);
size_t ucpset_difference_interval(uint32_t *dst,
                                  const uint32_t *x, size_t xcount,
                                  uint32_t lo, uint32_t hi);
size_t ucpset_xor_interval(uint32_t *dst,
                           const uint32_t *x, size_t xcount,
                           uint32_t lo, uint32_t hi);
```

These combine a set with the single interval `[lo, hi)`. Each finds the run of intervals that `[lo, hi)` affects and rewrites only that run, so the cost is a search plus one or two block moves, however large `x` is.

`dst` may be `x` itself, which makes the operation in place; otherwise it must not overlap `x`. An empty interval (`lo >= hi`) is the empty set.

**`ucpset_intersection_interval`** is the way to cut a set down to a range of codepoints, for example one plane:

```c
size_t n = ucpset_intersection_interval(dst, x, xcount, 0x10000, 0x20000);
```

**`ucpset_xor_interval`** toggles every codepoint in `[lo, hi)`.

`ucpset_union_interval` and `ucpset_difference_interval` with `dst == x` do the same as `ucpset_insert_interval` and `ucpset_remove_interval`.

To get the number of codepoints in a result, pass it to `ucpset_cardinality()`; see [Cardinality](#cardinality--ucpset_cardinalityh).

---

### In-place set operations — `ucpset_inplace.h`

```c
size_t ucpset_inplace_union(uint32_t *x, size_t xcount,
                            const uint32_t *y, size_t ycount);
size_t ucpset_inplace_intersection(uint32_t *x, size_t xcount,
                                   const uint32_t *y, size_t ycount);
size_t ucpset_inplace_difference(uint32_t *x, size_t xcount,
                                 const uint32_t *y, size_t ycount);
size_t ucpset_inplace_xor(uint32_t *x, size_t xcount,
                          const uint32_t *y, size_t ycount);
size_t ucpset_inplace_complement(uint32_t *x, size_t xcount,
                                 uint32_t lo, uint32_t hi);
```

Each function replaces `x` with the result and returns the new interval count. The spare capacity is working space: `x` is first moved up to make room, and the result is then written in front of it. So `x` needs the capacity listed under [Buffer capacity](#buffer-capacity), even when the result is smaller.

`y` may be the same pointer as `x`; it must not otherwise overlap `x`'s buffer.

The part of `x` that lies before `y` cannot change, so it is left where it is: an operation with a `y` near the end of a large `x` touches only the end. As with the out-of-place operations, when either set is a single interval the [one-interval](#one-interval) operations are used.

---

### Cardinality — `ucpset_cardinality.h`

```c
uint32_t ucpset_cardinality(const uint32_t *x, size_t xcount);
uint32_t ucpset_cardinality_intersection(const uint32_t *x, size_t xcount,
                                         const uint32_t *y, size_t ycount);
```

**`ucpset_cardinality`** returns the number of codepoints in a set.

**`ucpset_cardinality_intersection`** returns the number of codepoints two sets share, without building the intersection: it needs no output buffer.

The sizes of the other results follow from these two. `|x|` and `|y|` are one `ucpset_cardinality` call each, a linear pass; keep them if you need them more than once.

```
|x ∪ y| = |x| + |y| − |x ∩ y|
|x − y| = |x| − |x ∩ y|
|x △ y| = |x| + |y| − 2 |x ∩ y|
```

```c
uint32_t shared = ucpset_cardinality_intersection(x, xcount, y, ycount);
uint32_t either = ucpset_cardinality(x, xcount)
                + ucpset_cardinality(y, ycount) - shared;
```

---

### Predicates — `ucpset_predicate.h`

```c
bool ucpset_equal(const uint32_t *x, size_t xcount,
                  const uint32_t *y, size_t ycount);
bool ucpset_subset(const uint32_t *x, size_t xcount,
                   const uint32_t *y, size_t ycount);
bool ucpset_superset(const uint32_t *x, size_t xcount,
                     const uint32_t *y, size_t ycount);
bool ucpset_disjoint(const uint32_t *x, size_t xcount,
                     const uint32_t *y, size_t ycount);
bool ucpset_precedes(const uint32_t *x, size_t xcount,
                     const uint32_t *y, size_t ycount);
bool ucpset_succeeds(const uint32_t *x, size_t xcount,
                     const uint32_t *y, size_t ycount);

bool ucpset_pairwise_disjoint(const uint32_t *const *sets, const size_t *counts,
                              size_t k, size_t *scratch);
```

**`ucpset_equal`** returns `true` if the two sets contain the same codepoints. Since canonical form is unique, this is an array comparison.

**`ucpset_subset`** returns `true` if every codepoint of `x` is in `y`. **`ucpset_superset`** is the same test with the arguments swapped. The empty set is a subset of every set.

**`ucpset_disjoint`** returns `true` if the sets share no codepoint. The empty set is disjoint from every set.

**`ucpset_subset`** and **`ucpset_disjoint`** stop at the first counterexample.

**`ucpset_pairwise_disjoint`** returns `true` if no two of `k` sets share a codepoint. `sets[t]` is a set of `counts[t]` intervals, and `scratch` is working space for `2 * k` entries. It walks all the sets together, so sets that come in runs, such as the scripts of a Unicode version, cost one search per run instead of a test for every pair:

```c
const uint32_t *sets[]   = { latin, greek, cyrillic };
size_t          counts[] = { latin_count, greek_count, cyrillic_count };
size_t          scratch[2 * 3];

bool ok = ucpset_pairwise_disjoint(sets, counts, 3, scratch);
```

Declare the array as `const uint32_t *sets[]`: C does not convert an array of `uint32_t *` to `const uint32_t *const *` without a cast.

**`ucpset_precedes`** returns `true` if `x` sorts strictly before `y`: for non-empty sets, every codepoint of `x` is below every codepoint of `y`. The empty set sorts first: it precedes every non-empty set, nothing precedes it, and it does not precede itself. **`ucpset_succeeds`** is the mirror image, so `ucpset_precedes(x, y) == ucpset_succeeds(y, x)`.

---

### Ranges — `ucprange.h`

```c
typedef struct {
  uint32_t lo;
  uint32_t hi;
} ucprange_t;
```

`ucprange_t` is a single half-open range `[lo, hi)` passed by value. It has no dependency on the set headers and can be used on its own.

```c
ucprange_t ucprange(uint32_t lo, uint32_t hi);

uint32_t   ucprange_cardinality(ucprange_t r);
bool       ucprange_is_empty(ucprange_t r);
bool       ucprange_is_singleton(ucprange_t r);
bool       ucprange_is_valid(ucprange_t r);
bool       ucprange_contains(ucprange_t r, uint32_t value);

bool       ucprange_equals(ucprange_t x, ucprange_t y);
bool       ucprange_subset(ucprange_t x, ucprange_t y);
bool       ucprange_superset(ucprange_t x, ucprange_t y);
bool       ucprange_disjoint(ucprange_t x, ucprange_t y);
bool       ucprange_adjacent(ucprange_t x, ucprange_t y);
bool       ucprange_coalesce(ucprange_t x, ucprange_t y);
bool       ucprange_precedes(ucprange_t x, ucprange_t y);
bool       ucprange_succeeds(ucprange_t x, ucprange_t y);
int        ucprange_compare(ucprange_t x, ucprange_t y);
int        ucprange_relation(ucprange_t x, ucprange_t y);

ucprange_t ucprange_intersection(ucprange_t x, ucprange_t y);
ucprange_t ucprange_span(ucprange_t x, ucprange_t y);
ucprange_t ucprange_prefix(ucprange_t x, ucprange_t y);
ucprange_t ucprange_suffix(ucprange_t x, ucprange_t y);
ucprange_t ucprange_plus(ucprange_t r, uint32_t amount);
ucprange_t ucprange_minus(ucprange_t r, uint32_t amount);
```

**`ucprange_is_valid`** returns `true` if the range is non-empty and lies within Unicode: `lo < hi` and `hi <= 0x110000`.

**`ucprange_adjacent`** returns `true` if one range ends exactly where the other starts. **`ucprange_coalesce`** returns `true` if the two ranges overlap or are adjacent, that is, if their union is a single range.

**`ucprange_compare`** orders ranges by `lo`, then by `hi`, and returns `-1`, `0`, or `1`.

**`ucprange_relation`** classifies how `x` lies relative to `y`. The result is antisymmetric (`ucprange_relation(x, y) == -ucprange_relation(y, x)`), and a smaller magnitude means a closer relationship:

| Result | Meaning                                              |
|-------:|:-----------------------------------------------------|
| `-4`   | `x` precedes `y` with a gap                          |
| `-3`   | `x` is adjacent to `y` on the left (`x.hi == y.lo`)  |
| `-2`   | `x` overlaps `y` from the left                       |
| `-1`   | `x` is a proper superset of `y`                      |
| `0`    | `x` equals `y`                                       |
| `1`    | `x` is a proper subset of `y`                        |
| `2`    | `x` overlaps `y` from the right                      |
| `3`    | `x` is adjacent to `y` on the right (`x.lo == y.hi`) |
| `4`    | `x` succeeds `y` with a gap                          |

**`ucprange_intersection`** returns the part common to both ranges and **`ucprange_span`** the smallest range covering both.

**`ucprange_prefix`**, **`ucprange_intersection`**, and **`ucprange_suffix`** together split `x` into the part before `y`, the part inside `y`, and the part after `y`. Any of the three may be empty.

**`ucprange_plus`** and **`ucprange_minus`** shift both bounds by `amount`.

---

### Sets and ranges — `ucprange_set.h`

```c
size_t ucpset_insert_range(uint32_t *x, size_t xcount, ucprange_t range);
size_t ucpset_remove_range(uint32_t *x, size_t xcount, ucprange_t range);
bool   ucpset_contains_range(const uint32_t *x, size_t xcount, ucprange_t range);

size_t ucpset_union_range(uint32_t *dst, const uint32_t *x, size_t xcount,
                          ucprange_t range);
size_t ucpset_intersection_range(uint32_t *dst, const uint32_t *x, size_t xcount,
                                 ucprange_t range);
size_t ucpset_difference_range(uint32_t *dst, const uint32_t *x, size_t xcount,
                               ucprange_t range);
size_t ucpset_xor_range(uint32_t *dst, const uint32_t *x, size_t xcount,
                        ucprange_t range);

void   ucpset_find_overlap_range(const uint32_t *x, size_t xcount,
                                 ucprange_t range, size_t *i, size_t *j);
void   ucpset_find_coalesce_range(const uint32_t *x, size_t xcount,
                                  ucprange_t range, size_t *i, size_t *j);
```

These are the `ucprange_t` forms of the functions that take an interval as `lo` and `hi`: each `ucpset_*_range` forwards to the matching `ucpset_*_interval`. Behaviour is identical.

---

### Search — `ucpset_search.h`

```c
size_t ucpset_upper_bound_hi(const uint32_t *x, size_t xcount, uint32_t key);
size_t ucpset_lower_bound_hi(const uint32_t *x, size_t xcount, uint32_t key);
size_t ucpset_gallop_upper_bound_hi(const uint32_t *x, size_t xcount, uint32_t key);
size_t ucpset_gallop_lower_bound_hi(const uint32_t *x, size_t xcount, uint32_t key);

size_t ucpset_find(const uint32_t *x, size_t xcount, uint32_t v);
void   ucpset_find_overlap_interval(const uint32_t *x, size_t xcount,
                                    uint32_t lo, uint32_t hi,
                                    size_t *i, size_t *j);
void   ucpset_find_coalesce_interval(const uint32_t *x, size_t xcount,
                                     uint32_t lo, uint32_t hi,
                                     size_t *i, size_t *j);
```

These are the building blocks the other headers use, exposed for callers who need to locate an interval directly.

**`ucpset_upper_bound_hi`** returns the index of the first interval whose `hi` is greater than `key`, and **`ucpset_lower_bound_hi`** the first whose `hi` is greater than or equal to `key`. Both return `xcount` when there is none. The search is a branch-free binary search.

**`ucpset_gallop_upper_bound_hi`** and **`ucpset_gallop_lower_bound_hi`** return the same indices as `ucpset_upper_bound_hi` and `ucpset_lower_bound_hi` by exponential search from the front, so their cost depends on how far the answer is from the start rather than on `xcount`.

**`ucpset_find`** returns the index of the interval containing codepoint `v`, or `xcount` if `v` is not in the set.

**`ucpset_find_overlap_interval`** sets `[*i, *j)` to the run of intervals that share at least one codepoint with `[lo, hi)`. Only the first of them can start before `lo`, and only the last can end after `hi`.

**`ucpset_find_coalesce_interval`** sets `[*i, *j)` to the run of intervals that overlap or touch `[lo, hi)`: the ones a union with `[lo, hi)` would merge into one. When there are none, `*i == *j` is the position at which a non-empty `[lo, hi)` would be inserted.

## Adaptive galloping

A set operation normally steps through both inputs one interval at a time. That is the fastest way when the two sets interleave closely, but wasteful when one set is much sparser than the other, such as a single script tested against a large general category: most intervals of the larger set have nothing to interact with.

Union, intersection, and difference, `ucpset_cardinality_intersection`, and `ucpset_subset` and `ucpset_disjoint` therefore adapt. After `UCPSET_GALLOP_MIN` consecutive steps on the same side they gallop over the rest of that run, skipping it or copying it in one block. A run can be a stretch of one set that falls in a gap of the other, or a stretch that lies inside a single wide interval of the other. Stepping remains the default, because galloping on every step is slower when both sets are large.

The threshold defaults to 8 and can be set before including the library:

```c
#define UCPSET_GALLOP_MIN 16
#include "ucpset.h"
```

`ucpset_subset` and `ucpset_disjoint` adapt only when one set has at least `2 * UCPSET_GALLOP_MIN` times as many intervals as the other (the size condition). Sets of comparable size rarely produce runs long enough to gallop over, so they use a plain loop with no run counting at all. Union applies the same condition to the one place where counting is costly, the loop that absorbs intervals into a merged one.

Symmetric difference merges the two arrays boundary by boundary. Under the same size condition it copies a long run of the larger set's boundaries in one block; for sets of comparable size it is a plain merge.

The in-place operations run the same loops as their out-of-place forms and adapt in the same way.

## File overview

| File                   | Purpose                                                                 |
|:-----------------------|:------------------------------------------------------------------------|
| `ucpset.h`             | Umbrella header; includes everything below                              |
| `ucpset_canonical.h`   | Canonical form — `ucpset_canonical()`, `ucpset_normalize()`             |
| `ucpset_membership.h`  | Membership — `ucpset_contains()`, `ucpset_contains_hint()`, `ucpset_contains_interval()` |
| `ucpset_insert.h`      | In-place insertion — `ucpset_insert()`, `ucpset_insert_interval()`      |
| `ucpset_remove.h`      | In-place removal — `ucpset_remove()`, `ucpset_remove_interval()`        |
| `ucpset_merge.h`       | Out-of-place union, intersection, difference, xor, complement; `_interval` forms |
| `ucpset_inplace.h`     | In-place union, intersection, difference, xor, complement               |
| `ucpset_cardinality.h` | Codepoint count of a set, and of the intersection of two                |
| `ucpset_predicate.h`   | Equality, subset, superset, disjoint, precedes, succeeds                |
| `ucpset_splice.h`      | Replace a run of intervals in place — `ucpset_splice()`                 |
| `ucpset_slice.h`       | Copy a run of intervals — `ucpset_slice()`                              |
| `ucpset_search.h`      | Binary and galloping search, interval lookups; `UCPSET_GALLOP_MIN`      |
| `ucpset_sweep.h`       | Internal: the forward loop for each set operation                       |
| `ucpset_internal.h`    | Constants and block helpers — `ucpset_copy()`, `ucpset_move()`, `ucpset_open_run()` |
| `ucprange.h`           | Range value type — `ucprange_t` and its operations                      |
| `ucprange_set.h`       | `ucprange_t` forms of the interval functions                            |
| `bench/bench.c`        | Benchmark — `make bench`                                                |
| `bench/unicode_sets.h` | Unicode property sets for the benchmark, generated by `bench/gen_unicode_sets.pl` |

## Tests

```
make test     # build and run the test suite
make asan     # the same under AddressSanitizer and UndefinedBehaviorSanitizer
make ubsan    # the same under UndefinedBehaviorSanitizer alone
make cover    # line and branch coverage per header (gcov)
make report   # HTML coverage report in cover/ (gcovr)
```

Results are checked against a bit-vector model, where a set is one byte per codepoint and every operation is the obvious loop.

- **Exhaustive.** Every pair of sets over an 8-codepoint universe is run through every operation and predicate, along with every insertion, removal, and complement range.
- **Randomized.** Random sets over a 256-codepoint universe are run through the same checks.
- **Lopsided.** A dense set is paired with a sparse one, in both orders, so that the galloping paths are taken at the default threshold.

`make test` and `make asan` also build the suite a second time with `UCPSET_GALLOP_MIN=1`, which gallops at every opportunity so the small exhaustive sets reach those paths too.

## Benchmark

```
make bench                # build bench/bench and run every section
./bench/bench lookups     # one section: lookups, properties, random, queries, editing
```

`bench/bench.c` times the library on the inputs it is meant for and on inputs chosen to be hard. It needs only a C99 compiler and `clock_gettime`.

- **lookups** compares `ucpset_contains` with `ucpset_contains_hint`, per codepoint, over generated text: words in one script with ASCII spaces and punctuation between them, and a uniformly random stream for contrast.
- **properties** runs the four set operations and the complement, out-of-place and in-place, on pairs of Unicode properties: two of similar size, a large one against smaller ones, and a set against a single interval.
- **random** runs the same operations on sets of 30,000 intervals whose gaps and lengths are random, against each other, against a set of 300, and against a single interval near the start, in the middle and near the end.
- **queries** covers cardinality, the canonical check, and the predicates.
- **editing** covers inserting and removing a codepoint at different positions, and normalizing unsorted input.

The property sets are in `bench/unicode_sets.h`, which `bench/gen_unicode_sets.pl` generates from the Unicode Character Database that ships with perl.

How to read the figures:

- Every table has one unit, named in its heading, and each figure is the time of the call alone.
- Each figure is the fastest of nine timed batches on the same input, so caches are warm. Expect a few percent of variation between runs, and more between compilers and machines.
- An in-place operation overwrites its input, so each call runs on a fresh copy of `x`. The copies are made before the clock starts and are not part of the figure. Because that buffer has just been refilled, calls that take well under a microsecond (the single-interval rows) include a few cache misses that the out-of-place calls, which reread the same source every time, do not.
- In-place is the same sweep as out-of-place after `x` has been moved up to make room, so on two multi-interval sets it is never faster: level on balanced pairs, and up to about 40% slower in these runs when `y` is small and the move is a large share of the work. What it saves is the second buffer. With a single interval it moves only the intervals after the edit, so it gets faster the nearer the edit is to the end, while out-of-place always writes the whole result.
- Repeating one small input lets the processor's branch predictor learn it. The property sets cost around 1 ns per input interval in these runs; the first call on a set the processor has not seen will be slower. The random sets are the other extreme: there is nothing to learn, and a balanced pair costs 2.5 to 5 ns per input interval.
- `ucpset_contains_hint` pays off on text and loses on codepoints in random order, as the random BMP columns show.

A sample run, on an macOS machine on Apple silicon with Clang 21.1.8 and `-O2`:

```
ucpset benchmark: fastest of 9 batches of at least 2 ms each
compiler: Homebrew Clang 21.1.8
UCPSET_GALLOP_MIN: 8

Lookups: ns per codepoint
ucpset_contains and ucpset_contains_hint over 65536 codepoints of generated text

                        English          Greek            CJK              mixed scripts    random BMP
set (intervals)         contains   hint  contains   hint  contains   hint  contains   hint  contains   hint
Alphabetic (757)             9.0    2.4       8.8    2.0      10.0    4.6       9.0    2.2      10.1   22.6
L (677)                      8.7    2.2       8.9    2.0      10.0    4.5       8.7    2.2      10.1   22.9
Lu (651)                     8.7    2.2       8.9    2.0      10.0    2.0       8.7    2.2      10.1   22.3
Grapheme_Extend (375)        0.6    0.6       7.7    2.0       8.4    2.2       6.7    2.0       8.6   18.9
Nd (71)                      5.8    2.1       5.8    2.0       6.3    1.8       5.8    2.1       6.3    8.8
Latin (39)                   5.1    2.1       5.0    2.0       5.3    1.7       5.0    2.1       5.3   13.8
White_Space (10)             3.4    2.6       3.4    2.9       0.8    1.4       3.0    3.3       2.2    4.2

Set operations on Unicode properties (Unicode 16.0.0): ns per call

out-of-place: dst = x op y
x (intervals)       y (intervals)                        union  intersection    difference           xor
Lu (651)            Ll (662)                            1035.2        2106.4        1576.7        1271.5
Alphabetic (757)    Grapheme_Extend (375)               1064.9        1601.6        1729.5        3618.2
L (677)             Nd (71)                              494.9         689.7         942.9        2948.2
L (677)             Greek (36)                           241.9         192.7         317.5         277.6
Alphabetic (757)    Han (22)                             205.6         157.7         248.3         225.8
P (198)             ASCII (1)                             41.0          10.2          42.7          53.3

in-place: x = x op y
x (intervals)       y (intervals)                        union  intersection    difference           xor
Lu (651)            Ll (662)                            1170.3        2237.0        1721.8        1419.9
Alphabetic (757)    Grapheme_Extend (375)               1211.6        1755.3        1868.2        3751.0
L (677)             Nd (71)                              640.4         843.7        1064.5        3093.9
L (677)             Greek (36)                           371.9         327.6         426.4         401.7
Alphabetic (757)    Han (22)                             280.5         200.8         309.9         298.4
P (198)             ASCII (1)                             39.3           7.7          41.5          55.6

complement of x over all codepoints
x (intervals)       out-of-place      in-place
L (677)                     75.4         124.7

Set operations on large random sets: us per call

out-of-place: dst = x op y
x (intervals)       y (intervals)                        union  intersection    difference           xor
random (30000)      random (30000)                      157.75        278.25        233.94        252.12
random (30000)      random (300)                         14.25         19.57         22.66         19.48
random (300)        random (30000)                       15.00         20.04         18.48         19.99
random (30000)      interval near the start (1)           3.25          0.09          9.09          3.27
random (30000)      interval in the middle (1)            3.43          0.09          3.43          3.34
random (30000)      interval near the end (1)             3.52          0.09          3.49          3.47

in-place: x = x op y
x (intervals)       y (intervals)                        union  intersection    difference           xor
random (30000)      random (30000)                      159.52        281.48        237.42        254.73
random (30000)      random (300)                         18.33         22.89         26.54         23.89
random (300)        random (30000)                       15.27         19.56         18.50         20.75
random (30000)      interval near the start (1)           4.29          0.01          4.29          4.36
random (30000)      interval in the middle (1)            2.00          0.12          2.01          2.17
random (30000)      interval near the end (1)             0.15          0.13          0.15          0.17

complement of x over all codepoints
x (intervals)       out-of-place      in-place
random (30000)              3.85          4.29

Queries: ns per call

call                                                                  result           ns
cardinality(Alphabetic (757))                                         142759         75.3
cardinality_intersection(Alphabetic (757), Grapheme_Extend (375))     965          1590.8
cardinality_intersection(L (677), Greek (36))                         350           195.1
canonical(Alphabetic (757))                                           true          592.5
equal(L (677), a copy of it)                                          true          147.5
subset(Lu (651), L (677))                                             true          957.0
subset(Greek letters (36), L (677))                                   true           95.8
subset(Nd (71), L (677))                                              false           1.2
disjoint(Lu (651), Ll (662))                                          true         1300.3
disjoint(L (677), Nd (71))                                            true          651.6
disjoint(Latin (39), Greek (36))                                      true           58.9
disjoint(L (677), Alphabetic (757))                                   false           2.0
pairwise_disjoint(all 170 scripts)                                    true         8543.0
  the same with disjoint on each of the 14365 pairs                   true        82406.2

Insert a codepoint as a new interval, then remove it: ns per pair of calls

call                                                                  result           ns
Alphabetic (757), near the start                                                    239.4
Alphabetic (757), in the middle                                                     147.5
Alphabetic (757), past the end                                                        3.4
random (30000), near the start                                                     7937.5
random (30000), in the middle                                                      3401.4
random (30000), past the end                                                          3.4

Normalize: us per call

call                                                                  result           us
normalize(10000 random intervals, unsorted)                           7416         368.49
normalize(7416 intervals, already canonical)                          7416          10.34
normalize_codepoints(10000 codepoints of mixed-script text)           1417         300.88
```

## License

MIT License. Copyright (c) 2026 Christian Hansen.
