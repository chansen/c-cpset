#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include "ucpset.h"

// =========================== Test infrastructure ===========================

#define DIE(...) do { \
    fprintf(stderr, "TEST FAILURE: " __VA_ARGS__); \
    fprintf(stderr, " @ %s:%d\n", __FILE__, __LINE__); \
    exit(1); \
} while (0)

#define ASSERT(cond, ...) do { if (!(cond)) { DIE(__VA_ARGS__); } } while (0)

static void validate_intervals(const uint32_t* arr, size_t count, const char* label) {
  for (size_t i = 0; i < count; i++) {
    uint32_t lo = arr[i * 2];
    uint32_t hi = arr[i * 2 + 1];
    ASSERT(lo < hi, "%s: empty/reversed interval %zu: [%u, %u)", label, i, lo, hi);
    ASSERT(hi <= UCPSET_BOUND_MAX, "%s: hi %u exceeds Unicode max", label, hi);
    if (i + 1 < count) {
      uint32_t next_lo = arr[(i + 1) * 2];
      ASSERT(hi < next_lo,
             "%s: not strictly canonical at %zu: [%u, %u) [%u, ...)",
             label, i, lo, hi, next_lo);
    }
  }
}

static bool intervals_equal(const uint32_t* a, size_t ac, const uint32_t* b, size_t bc) {
  if (ac != bc) return false;
  return ac == 0 || memcmp(a, b, ac * 2 * sizeof(uint32_t)) == 0;
}

static void expect_eq(const char* label,
                      const uint32_t* got, size_t gc,
                      const uint32_t* exp, size_t ec) {
  if (!intervals_equal(got, gc, exp, ec)) {
    fprintf(stderr, "Expected %s (%zu intervals):", label, ec);
    for (size_t i = 0; i < ec; i++)
      fprintf(stderr, " [%u,%u)", exp[i*2], exp[i*2+1]);
    fprintf(stderr, "\nGot (%zu intervals):", gc);
    for (size_t i = 0; i < gc; i++)
      fprintf(stderr, " [%u,%u)", got[i*2], got[i*2+1]);
    fprintf(stderr, "\n");
    DIE("mismatch");
  }
}

// ========================== Bitvec ground truth ============================

typedef struct {
  size_t N;
  uint8_t* bits;
} bitvec_t;

static bitvec_t bv_make(size_t N) {
  bitvec_t b = { .N = N, .bits = (uint8_t*)calloc(N, 1) };
  ASSERT(b.bits, "OOM");
  return b;
}

static void bv_free(bitvec_t* b) {
  free(b->bits);
  b->bits = NULL;
}

static void bv_clear(bitvec_t* b) {
  memset(b->bits, 0, b->N);
}

static void bv_from_intervals(bitvec_t* b, const uint32_t* arr, size_t count) {
  bv_clear(b);
  for (size_t i = 0; i < count; i++) {
    uint32_t lo = arr[i*2];
    uint32_t hi = arr[i*2+1];
    ASSERT(hi <= b->N, "interval exceeds domain");
    for (uint32_t x = lo; x < hi; x++)
      b->bits[x] = 1;
  }
}

static size_t bv_to_intervals(const bitvec_t* b, uint32_t* out) {
  size_t count = 0;
  size_t i = 0;
  while (i < b->N) {
    while (i < b->N && !b->bits[i])
      i++;
    if (i >= b->N)
      break;
    uint32_t lo = (uint32_t)i;
    while (i < b->N && b->bits[i])
      i++;
    out[count*2] = lo;
    out[count*2+1] = (uint32_t)i;
    count++;
  }
  return count;
}

static uint32_t bv_cardinality(const bitvec_t* b) {
  uint32_t c = 0;
  for (size_t i = 0; i < b->N; i++)
    if (b->bits[i]) c++;
  return c;
}

static void bv_union(bitvec_t* dst, const bitvec_t* a, const bitvec_t* b) {
  for (size_t i = 0; i < dst->N; i++)
    dst->bits[i] = a->bits[i] | b->bits[i];
}

static void bv_intersection(bitvec_t* dst, const bitvec_t* a, const bitvec_t* b) {
  for (size_t i = 0; i < dst->N; i++)
    dst->bits[i] = a->bits[i] & b->bits[i];
}

static void bv_difference(bitvec_t* dst, const bitvec_t* a, const bitvec_t* b) {
  for (size_t i = 0; i < dst->N; i++)
    dst->bits[i] = a->bits[i] & ~b->bits[i];
}

static void bv_xor(bitvec_t* dst, const bitvec_t* a, const bitvec_t* b) {
  for (size_t i = 0; i < dst->N; i++)
    dst->bits[i] = a->bits[i] ^ b->bits[i];
}

static void bv_complement(bitvec_t* dst, const bitvec_t* a, uint32_t lo, uint32_t hi) {
  bv_clear(dst);
  for (uint32_t i = lo; i < hi && i < dst->N; i++)
    dst->bits[i] = !a->bits[i];
}

// Simple PRNG (xoshiro128**)
static uint32_t rng_state[4] = {0x12345678, 0x9ABCDEF0, 0xDEADBEEF, 0xCAFEBABE};

static inline uint32_t rotl(uint32_t x, int k) {
  return (x << k) | (x >> (32 - k));
}

static uint32_t rng_next(void) {
  uint32_t result = rotl(rng_state[1] * 5, 7) * 9;
  uint32_t t = rng_state[1] << 9;
  rng_state[2] ^= rng_state[0];
  rng_state[3] ^= rng_state[1];
  rng_state[1] ^= rng_state[2];
  rng_state[0] ^= rng_state[3];
  rng_state[2] ^= t;
  rng_state[3] = rotl(rng_state[3], 11);
  return result;
}

static uint32_t rng_bounded(uint32_t bound) {
  return rng_next() % bound;
}

// Generate a random strictly canonical set over [0, N)
static size_t random_set(uint32_t* buf, size_t N) {
  size_t count = 0;
  size_t i = 0;
  while (i < N) {
    if (rng_bounded(3) == 0) {
      uint32_t lo = (uint32_t)i;
      size_t len = 1 + rng_bounded(4);
      if (lo + len > N) len = N - lo;
      uint32_t hi = (uint32_t)(lo + len);
      buf[count*2] = lo;
      buf[count*2+1] = hi;
      count++;
      i = (size_t)hi + 1;  // +1 ensures non-adjacent
    } else {
      i++;
    }
  }
  return count;
}

// =========================== Canonicality tests ============================

static void test_canonicality(void) {
  // empty is canonical
  ASSERT(ucpset_canonical(NULL, 0), "empty should be canonical");

  // single interval
  {
    uint32_t x[] = {0, 5};
    ASSERT(ucpset_canonical(x, 1), "single interval");
  }

  // two non-adjacent intervals
  {
    uint32_t x[] = {0, 5, 10, 15};
    ASSERT(ucpset_canonical(x, 2), "non-adjacent intervals");
  }

  // adjacent intervals (hi == next_lo)
  {
    uint32_t x[] = {0, 5, 5, 10};
    ASSERT(!ucpset_canonical(x, 2), "adjacent intervals not canonical");
  }

  // overlapping
  {
    uint32_t x[] = {0, 10, 5, 15};
    ASSERT(!ucpset_canonical(x, 2), "overlapping not canonical");
  }

  // reversed
  {
    uint32_t x[] = {5, 3};
    ASSERT(!ucpset_canonical(x, 1), "reversed not canonical");
  }

  // empty interval
  {
    uint32_t x[] = {5, 5};
    ASSERT(!ucpset_canonical(x, 1), "empty interval not canonical");
  }

  // out of Unicode range
  {
    uint32_t x[] = {0, 0x120000};
    ASSERT(!ucpset_canonical(x, 1), "out of unicode range");
  }

  // max valid
  {
    uint32_t x[] = {0x10FFFF, 0x110000};
    ASSERT(ucpset_canonical(x, 1), "max codepoint valid");
  }
}

// =============================== Union tests ===============================

static void test_union_manual(void) {
  uint32_t result[16];

  // empty union empty
  {
    size_t n = ucpset_union(result, NULL, 0, NULL, 0);
    ASSERT(n == 0, "union: empty u empty = empty");
  }

  // empty union A
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_union(result, NULL, 0, a, 1);
    expect_eq("union: empty u A", result, n, a, 1);
  }

  // A union empty
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_union(result, a, 1, NULL, 0);
    expect_eq("union: A u empty", result, n, a, 1);
  }

  // identical
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_union(result, a, 1, a, 1);
    expect_eq("union: A u A", result, n, a, 1);
  }

  // disjoint
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {30, 40};
    size_t n = ucpset_union(result, a, 1, b, 1);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("union: disjoint", result, n, exp, 2);
  }

  // touching (disjoint inputs, produce single merged output)
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {20, 30};
    size_t n = ucpset_union(result, a, 1, b, 1);
    uint32_t exp[] = {10, 30};
    expect_eq("union: touching", result, n, exp, 1);
  }

  // overlapping
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {15, 25};
    size_t n = ucpset_union(result, a, 1, b, 1);
    uint32_t exp[] = {10, 25};
    expect_eq("union: overlapping", result, n, exp, 1);
  }

  // one contains other
  {
    uint32_t a[] = {10, 20};
    uint32_t big[] = {5, 25};
    size_t n = ucpset_union(result, a, 1, big, 1);
    expect_eq("union: contained", result, n, big, 1);
  }

  // multi-interval
  {
    uint32_t a[] = {0, 5, 10, 15, 20, 25};
    uint32_t b[] = {3, 12, 18, 22};
    size_t n = ucpset_union(result, a, 3, b, 2);
    uint32_t exp[] = {0, 15, 18, 25};
    expect_eq("union: multi", result, n, exp, 2);
    validate_intervals(result, n, "union multi");
  }
}

// =========================== Intersection tests ============================

static void test_intersection_manual(void) {
  uint32_t result[16];

  // empty
  {
    size_t n = ucpset_intersection(result, NULL, 0, NULL, 0);
    ASSERT(n == 0, "intersection: empty");
  }

  // A intersect empty
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_intersection(result, a, 1, NULL, 0);
    ASSERT(n == 0, "intersection: A n empty");
  }

  // disjoint
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {30, 40};
    size_t n = ucpset_intersection(result, a, 1, b, 1);
    ASSERT(n == 0, "intersection: disjoint");
  }

  // touching
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {20, 30};
    size_t n = ucpset_intersection(result, a, 1, b, 1);
    ASSERT(n == 0, "intersection: touching");
  }

  // overlapping
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {15, 25};
    size_t n = ucpset_intersection(result, a, 1, b, 1);
    uint32_t exp[] = {15, 20};
    expect_eq("intersection: overlap", result, n, exp, 1);
  }

  // contained
  {
    uint32_t a[] = {10, 20};
    uint32_t big[] = {5, 25};
    size_t n = ucpset_intersection(result, a, 1, big, 1);
    expect_eq("intersection: contained", result, n, a, 1);
  }
}

// ============================ Difference tests =============================

static void test_difference_manual(void) {
  uint32_t result[16];

  // A \ empty
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_difference(result, a, 1, NULL, 0);
    expect_eq("diff: A \\ empty", result, n, a, 1);
  }

  // empty \ A
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_difference(result, NULL, 0, a, 1);
    ASSERT(n == 0, "diff: empty \\ A");
  }

  // self
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_difference(result, a, 1, a, 1);
    ASSERT(n == 0, "diff: A \\ A");
  }

  // disjoint
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {30, 40};
    size_t n = ucpset_difference(result, a, 1, b, 1);
    expect_eq("diff: disjoint", result, n, a, 1);
  }

  // overlapping
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {15, 25};
    size_t n = ucpset_difference(result, a, 1, b, 1);
    uint32_t exp[] = {10, 15};
    expect_eq("diff: overlap", result, n, exp, 1);
  }

  // punch hole
  {
    uint32_t a[] = {10, 20};
    uint32_t hole[] = {13, 17};
    size_t n = ucpset_difference(result, a, 1, hole, 1);
    uint32_t exp[] = {10, 13, 17, 20};
    expect_eq("diff: hole", result, n, exp, 2);
  }
}

// ================================ XOR tests ================================

static void test_xor_manual(void) {
  uint32_t result[16];

  // A xor empty
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_xor(result, a, 1, NULL, 0);
    expect_eq("xor: A ^ empty", result, n, a, 1);
  }

  // A xor A
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_xor(result, a, 1, a, 1);
    ASSERT(n == 0, "xor: A ^ A");
  }

  // overlapping
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {15, 25};
    size_t n = ucpset_xor(result, a, 1, b, 1);
    uint32_t exp[] = {10, 15, 20, 25};
    expect_eq("xor: overlap", result, n, exp, 2);
  }

  // touching
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {20, 30};
    size_t n = ucpset_xor(result, a, 1, b, 1);
    uint32_t exp[] = {10, 30};
    expect_eq("xor: touching", result, n, exp, 1);
  }
}

// ============================ Complement tests =============================

static void test_complement(void) {
  uint32_t result[16];

  // complement of empty in [0, 100)
  {
    size_t n = ucpset_complement(result, NULL, 0, 0, 100);
    uint32_t exp[] = {0, 100};
    expect_eq("complement: empty", result, n, exp, 1);
  }

  // complement of [10, 20) in [0, 30)
  {
    uint32_t a[] = {10, 20};
    size_t n = ucpset_complement(result, a, 1, 0, 30);
    uint32_t exp[] = {0, 10, 20, 30};
    expect_eq("complement: one", result, n, exp, 2);
  }

  // complement of full range
  {
    uint32_t full[] = {0, 100};
    size_t n = ucpset_complement(result, full, 1, 0, 100);
    ASSERT(n == 0, "complement: full");
  }

  // multi-interval complement
  {
    uint32_t m[] = {10, 20, 30, 40, 50, 60};
    size_t n = ucpset_complement(result, m, 3, 0, 70);
    uint32_t exp[] = {0, 10, 20, 30, 40, 50, 60, 70};
    expect_eq("complement: multi", result, n, exp, 4);
    validate_intervals(result, n, "complement multi");
  }

  // empty universe (lo >= hi) — all complement variants must return 0
  {
    uint32_t a[] = {5, 10};

    // ucpset_complement: lo == hi
    size_t n = ucpset_complement(result, a, 1, 5, 5);
    ASSERT(n == 0, "complement: lo==hi → 0 intervals");

    // ucpset_complement: lo > hi
    n = ucpset_complement(result, a, 1, 10, 5);
    ASSERT(n == 0, "complement: lo>hi → 0 intervals");

    // ucpset_inplace_complement: lo == hi
    uint32_t buf[16] = {5, 10};
    n = ucpset_inplace_complement(buf, 1, 5, 5);
    ASSERT(n == 0, "inplace_complement: lo==hi → 0");

    // ucpset_inplace_complement: lo > hi
    buf[0] = 5; buf[1] = 10;
    n = ucpset_inplace_complement(buf, 1, 10, 5);
    ASSERT(n == 0, "inplace_complement: lo>hi → 0");

    // empty set with empty universe
    n = ucpset_complement(result, NULL, 0, 7, 7);
    ASSERT(n == 0, "complement: empty set, lo==hi → 0");
  }
}

// ============================= Predicate tests =============================

static void test_predicates(void) {
  // equal
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {10, 20};
    ASSERT(ucpset_equal(a, 1, b, 1), "equal: identical");
    ASSERT(ucpset_equal(NULL, 0, NULL, 0), "equal: both empty");
  }

  // not equal
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {5, 25};
    ASSERT(!ucpset_equal(a, 1, b, 1), "equal: different");
  }

  // subset / superset
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {5, 25};
    ASSERT(ucpset_subset(a, 1, b, 1), "subset: A sub B");
    ASSERT(!ucpset_subset(b, 1, a, 1), "subset: B not sub A");
    ASSERT(ucpset_subset(NULL, 0, a, 1), "subset: empty sub A");
    ASSERT(ucpset_superset(b, 1, a, 1), "superset: B sup A");
  }

  // disjoint
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {30, 40};
    uint32_t c[] = {5, 25};
    ASSERT(ucpset_disjoint(a, 1, b, 1), "disjoint: yes");
    ASSERT(!ucpset_disjoint(a, 1, c, 1), "disjoint: no");
    ASSERT(ucpset_disjoint(NULL, 0, a, 1), "disjoint: empty");
  }

  // precedes / succeeds
  {
    uint32_t a[] = {10, 20};
    uint32_t b[] = {30, 40};
    ASSERT(ucpset_precedes(a, 1, b, 1), "precedes: A < B");
    ASSERT(!ucpset_precedes(b, 1, a, 1), "precedes: B not < A");
    ASSERT(ucpset_succeeds(b, 1, a, 1), "succeeds: B > A");
    ASSERT(ucpset_precedes(NULL, 0, a, 1), "precedes: empty < A");
    ASSERT(!ucpset_precedes(NULL, 0, NULL, 0), "precedes: empty !< empty");
  }
}

// ============================ Membership tests =============================

static void test_membership(void) {
  // single-element contains
  {
    uint32_t x[] = {10, 20, 30, 40};
    ASSERT(ucpset_contains(x, 2, 10), "contains 10");
    ASSERT(ucpset_contains(x, 2, 19), "contains 19");
    ASSERT(!ucpset_contains(x, 2, 20), "not contains 20");
    ASSERT(!ucpset_contains(x, 2, 25), "not contains 25");
    ASSERT(ucpset_contains(x, 2, 30), "contains 30");
    ASSERT(!ucpset_contains(x, 2, 40), "not contains 40");
    ASSERT(!ucpset_contains(x, 2, 5), "not contains 5");
  }

  // contains interval
  {
    uint32_t x[] = {10, 20, 30, 40};
    ASSERT(ucpset_contains_interval(x, 2, 10, 20), "contains [10,20)");
    ASSERT(ucpset_contains_interval(x, 2, 12, 18), "contains [12,18)");
    ASSERT(!ucpset_contains_interval(x, 2, 10, 21), "not contains [10,21)");
    ASSERT(!ucpset_contains_interval(x, 2, 15, 35), "not contains [15,35)");
    ASSERT(!ucpset_contains_interval(x, 2, 20, 30), "not contains [20,30) gap");
  }
}

// ============================== Insert tests ===============================

static void test_insert(void) {
  // insert into empty
  {
    uint32_t buf[20];
    size_t n = ucpset_insert(buf, 0, 5);
    uint32_t exp[] = {5, 6};
    expect_eq("insert: empty", buf, n, exp, 1);
  }

  // insert adjacent (merge right) and extend
  {
    uint32_t buf[20] = {5, 6};
    size_t n = 1;
    n = ucpset_insert(buf, n, 4);
    uint32_t exp1[] = {4, 6};
    expect_eq("insert: merge right", buf, n, exp1, 1);

    n = ucpset_insert(buf, n, 6);
    uint32_t exp2[] = {4, 7};
    expect_eq("insert: extend", buf, n, exp2, 1);
  }

  // insert already contained
  {
    uint32_t buf[] = {4, 7};
    size_t n = ucpset_insert(buf, 1, 5);
    ASSERT(n == 1, "insert: no-op");
  }

  // insert bridging two intervals
  {
    uint32_t buf[20] = {10, 15, 20, 25};
    size_t n = 2;
    for (uint32_t v = 15; v < 20; v++)
      n = ucpset_insert(buf, n, v);
    uint32_t exp[] = {10, 25};
    expect_eq("insert: bridge", buf, n, exp, 1);
  }

  // insert beyond max codepoint
  {
    uint32_t buf[4];
    size_t n = ucpset_insert(buf, 0, 0x110000);
    ASSERT(n == 0, "insert: beyond max no-op");
  }

  // insert max codepoint (0x10FFFF) — should work
  {
    uint32_t buf[4];
    size_t n = ucpset_insert(buf, 0, 0x10FFFF);
    uint32_t exp[] = {0x10FFFF, 0x110000};
    expect_eq("insert: max codepoint", buf, n, exp, 1);
  }
}

// ========================== Insert interval tests ==========================

static void test_insert_interval(void) {
  // insert into empty
  {
    uint32_t buf[20];
    size_t n = ucpset_insert_interval(buf, 0, 10, 20);
    uint32_t exp[] = {10, 20};
    expect_eq("insert_interval: empty", buf, n, exp, 1);
  }

  // insert overlapping
  {
    uint32_t buf[20] = {10, 20};
    size_t n = ucpset_insert_interval(buf, 1, 15, 25);
    uint32_t exp[] = {10, 25};
    expect_eq("insert_interval: overlap", buf, n, exp, 1);
  }

  // insert disjoint after
  {
    uint32_t buf[20] = {10, 25};
    size_t n = ucpset_insert_interval(buf, 1, 30, 40);
    uint32_t exp[] = {10, 25, 30, 40};
    expect_eq("insert_interval: disjoint", buf, n, exp, 2);
  }

  // insert bridging
  {
    uint32_t buf[20] = {10, 25, 30, 40};
    size_t n = ucpset_insert_interval(buf, 2, 25, 30);
    uint32_t exp[] = {10, 40};
    expect_eq("insert_interval: bridge", buf, n, exp, 1);
  }

  // no-op (reversed range)
  {
    uint32_t buf[] = {10, 40};
    size_t n = ucpset_insert_interval(buf, 1, 20, 10);
    uint32_t exp[] = {10, 40};
    expect_eq("insert_interval: noop", buf, n, exp, 1);
  }
}

// ============================== Remove tests ===============================

static void test_remove(void) {
  // remove from empty
  {
    uint32_t buf[4];
    size_t n = ucpset_remove(buf, 0, 5);
    ASSERT(n == 0, "remove: empty");
  }

  // remove left edge
  {
    uint32_t buf[] = {10, 20, 0, 0};
    size_t n = ucpset_remove(buf, 1, 10);
    uint32_t exp[] = {11, 20};
    expect_eq("remove: left edge", buf, n, exp, 1);
  }

  // remove right edge
  {
    uint32_t buf[] = {11, 20, 0, 0};
    size_t n = ucpset_remove(buf, 1, 19);
    uint32_t exp[] = {11, 19};
    expect_eq("remove: right edge", buf, n, exp, 1);
  }

  // remove middle — split
  {
    uint32_t buf[] = {11, 19, 0, 0, 0, 0};
    size_t n = ucpset_remove(buf, 1, 15);
    uint32_t exp[] = {11, 15, 16, 19};
    expect_eq("remove: split", buf, n, exp, 2);
  }

  // remove singleton
  {
    uint32_t buf[] = {5, 6, 0, 0};
    size_t n = ucpset_remove(buf, 1, 5);
    ASSERT(n == 0, "remove: singleton");
  }

  // remove not in set
  {
    uint32_t buf[] = {10, 20};
    size_t n = ucpset_remove(buf, 1, 25);
    ASSERT(n == 1, "remove: not in set");
  }

  // remove beyond max codepoint
  {
    uint32_t buf[] = {0x10FFFF, 0x110000};
    size_t n = ucpset_remove(buf, 1, 0x110000);
    ASSERT(n == 1, "remove: beyond max no-op");
    ASSERT(buf[0] == 0x10FFFF && buf[1] == 0x110000,
           "remove: beyond max unchanged");
  }

  // remove max codepoint (0x10FFFF) — should work
  {
    uint32_t buf[] = {0x10FFFF, 0x110000, 0, 0};
    size_t n = ucpset_remove(buf, 1, 0x10FFFF);
    ASSERT(n == 0, "remove: max codepoint singleton");
  }
}

// ========================== Remove interval tests ==========================

static void test_remove_interval(void) {
  // remove from middle — split
  {
    uint32_t buf[] = {10, 20, 0, 0, 0, 0};
    size_t n = ucpset_remove_interval(buf, 1, 13, 17);
    uint32_t exp[] = {10, 13, 17, 20};
    expect_eq("remove_interval: split", buf, n, exp, 2);
  }

  // remove spanning multiple
  {
    uint32_t buf[] = {10, 20, 30, 40, 50, 60, 0, 0};
    size_t n = ucpset_remove_interval(buf, 3, 15, 55);
    uint32_t exp[] = {10, 15, 55, 60};
    expect_eq("remove_interval: spanning", buf, n, exp, 2);
  }

  // remove everything
  {
    uint32_t buf[] = {10, 20, 0, 0};
    size_t n = ucpset_remove_interval(buf, 1, 0, 100);
    ASSERT(n == 0, "remove_interval: all");
  }
}

// ============================ Cardinality tests ============================

static void test_cardinality(void) {
  // empty
  ASSERT(ucpset_cardinality(NULL, 0) == 0, "card: empty");

  // two intervals
  {
    uint32_t a[] = {10, 20, 30, 40};
    ASSERT(ucpset_cardinality(a, 2) == 20, "card: 20");
  }

  // single interval
  {
    uint32_t a[] = {0, 100};
    ASSERT(ucpset_cardinality(a, 1) == 100, "card: 100");
  }
}

// ========================= Boolean identity tests ==========================

static void test_boolean_identities(void) {
  uint32_t a[] = {0, 10, 20, 30, 40, 50};
  uint32_t b[] = {5, 15, 25, 35, 45, 55};
  uint32_t r1[32], r2[32], r3[32];
  size_t n1, n2, n3;

  // A u B == B u A
  n1 = ucpset_union(r1, a, 3, b, 3);
  n2 = ucpset_union(r2, b, 3, a, 3);
  ASSERT(intervals_equal(r1, n1, r2, n2), "union commutative");

  // A n B == B n A
  n1 = ucpset_intersection(r1, a, 3, b, 3);
  n2 = ucpset_intersection(r2, b, 3, a, 3);
  ASSERT(intervals_equal(r1, n1, r2, n2), "intersection commutative");

  // A xor B == B xor A
  n1 = ucpset_xor(r1, a, 3, b, 3);
  n2 = ucpset_xor(r2, b, 3, a, 3);
  ASSERT(intervals_equal(r1, n1, r2, n2), "xor commutative");

  // |A u B| = |A| + |B| - |A n B| (inclusion-exclusion)
  {
    uint32_t card_union = ucpset_cardinality(r1, ucpset_union(r1, a, 3, b, 3));
    uint32_t card_a = ucpset_cardinality(a, 3);
    uint32_t card_b = ucpset_cardinality(b, 3);
    uint32_t card_inter = ucpset_cardinality_intersection(a, 3, b, 3);
    ASSERT(card_union == card_a + card_b - card_inter, "inclusion-exclusion");
  }

  // A \ B == A n complement(B) over [0, 60)
  n1 = ucpset_difference(r1, a, 3, b, 3);
  n2 = ucpset_complement(r2, b, 3, 0, 60);
  n3 = ucpset_intersection(r3, a, 3, r2, n2);
  ASSERT(intervals_equal(r1, n1, r3, n3), "diff == inter complement");
}

// ====================== Exhaustive small-domain tests ======================

static void test_exhaustive_small(void) {
  const size_t N = 8;
  bitvec_t bx = bv_make(N);
  bitvec_t by = bv_make(N);
  bitvec_t btmp = bv_make(N);
  uint32_t xr[2*N], yr[2*N];

  for (unsigned xm = 0; xm < (1u << N); xm++) {
    bv_clear(&bx);
    for (size_t i = 0; i < N; i++)
      if (xm & (1u << i)) bx.bits[i] = 1;
    size_t xcount = bv_to_intervals(&bx, xr);

    for (unsigned ym = 0; ym < (1u << N); ym++) {
      bv_clear(&by);
      for (size_t i = 0; i < N; i++)
        if (ym & (1u << i)) by.bits[i] = 1;
      size_t ycount = bv_to_intervals(&by, yr);

      // out-of-place union
      {
        bv_union(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t result[2*N];
        size_t got_count = ucpset_union(result, xr, xcount, yr, ycount);
        ASSERT(intervals_equal(result, got_count, expected, exp_count),
               "exhaustive union xm=%u ym=%u", xm, ym);
        validate_intervals(result, got_count, "exhaustive union");
      }

      // out-of-place intersection
      {
        bv_intersection(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t result[2*N];
        size_t got_count = ucpset_intersection(result, xr, xcount, yr, ycount);
        ASSERT(intervals_equal(result, got_count, expected, exp_count),
               "exhaustive intersection xm=%u ym=%u", xm, ym);
        validate_intervals(result, got_count, "exhaustive intersection");

        uint32_t exp_card = bv_cardinality(&btmp);
        ASSERT(ucpset_cardinality_intersection(xr, xcount, yr, ycount) == exp_card,
               "exhaustive intersection cardinality xm=%u ym=%u", xm, ym);
      }

      // out-of-place difference
      {
        bv_difference(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t result[2*N];
        size_t got_count = ucpset_difference(result, xr, xcount, yr, ycount);
        ASSERT(intervals_equal(result, got_count, expected, exp_count),
               "exhaustive difference xm=%u ym=%u", xm, ym);
        validate_intervals(result, got_count, "exhaustive difference");
      }

      // out-of-place xor
      {
        bv_xor(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t result[2*N];
        size_t got_count = ucpset_xor(result, xr, xcount, yr, ycount);
        ASSERT(intervals_equal(result, got_count, expected, exp_count),
               "exhaustive xor xm=%u ym=%u", xm, ym);
        validate_intervals(result, got_count, "exhaustive xor");
      }

      // in-place union
      {
        bv_union(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t exp_card = bv_cardinality(&btmp);
        uint32_t buf[4*N];
        uint32_t got_card;
        memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
        size_t got = ucpset_inplace_union(buf, xcount, yr, ycount);
        got_card = ucpset_cardinality(buf, got);
        ASSERT(intervals_equal(buf, got, expected, exp_count),
               "exhaustive inplace union xm=%u ym=%u", xm, ym);
        ASSERT(got_card == exp_card,
               "exhaustive inplace union card xm=%u ym=%u", xm, ym);
      }

      // in-place intersection
      {
        bv_intersection(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t exp_card = bv_cardinality(&btmp);
        uint32_t buf[4*N];
        uint32_t got_card;
        memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
        size_t got = ucpset_inplace_intersection(buf, xcount, yr, ycount);
        got_card = ucpset_cardinality(buf, got);
        ASSERT(intervals_equal(buf, got, expected, exp_count),
               "exhaustive inplace intersection xm=%u ym=%u", xm, ym);
        ASSERT(got_card == exp_card,
               "exhaustive inplace intersection card xm=%u ym=%u", xm, ym);
      }

      // in-place difference
      {
        bv_difference(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t exp_card = bv_cardinality(&btmp);
        uint32_t buf[4*N];
        uint32_t got_card;
        memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
        size_t got = ucpset_inplace_difference(buf, xcount, yr, ycount);
        got_card = ucpset_cardinality(buf, got);
        ASSERT(intervals_equal(buf, got, expected, exp_count),
               "exhaustive inplace difference xm=%u ym=%u", xm, ym);
        ASSERT(got_card == exp_card,
               "exhaustive inplace difference card xm=%u ym=%u", xm, ym);
      }

      // in-place xor
      {
        bv_xor(&btmp, &bx, &by);
        uint32_t expected[2*N];
        size_t exp_count = bv_to_intervals(&btmp, expected);
        uint32_t exp_card = bv_cardinality(&btmp);
        uint32_t buf[4*N];
        uint32_t got_card;
        memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
        size_t got = ucpset_inplace_xor(buf, xcount, yr, ycount);
        got_card = ucpset_cardinality(buf, got);
        ASSERT(intervals_equal(buf, got, expected, exp_count),
               "exhaustive inplace xor xm=%u ym=%u", xm, ym);
        ASSERT(got_card == exp_card,
               "exhaustive inplace xor card xm=%u ym=%u", xm, ym);
      }

      // complement for all universe slices [lo, hi)
      for (size_t lo = 0; lo < N; lo++) {
        for (size_t hi = lo + 1; hi <= N; hi++) {
          bv_clear(&btmp);
          for (size_t p = lo; p < hi; p++)
            btmp.bits[p] = bx.bits[p] ? 0 : 1;

          uint32_t exp[2*N];
          size_t exp_count = bv_to_intervals(&btmp, exp);
          uint32_t exp_card = bv_cardinality(&btmp);

          // out-of-place complement
          {
            uint32_t result[2*N];
            size_t n = ucpset_complement(result, xr, xcount,
                                         (uint32_t)lo, (uint32_t)hi);
            ASSERT(intervals_equal(result, n, exp, exp_count),
                   "exhaustive complement xm=%u lo=%zu hi=%zu", xm, lo, hi);
          }

          // in-place complement
          {
            uint32_t buf[2*N + 2];
            uint32_t got_card;
            memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
            size_t n = ucpset_inplace_complement(buf, xcount,
                                                 (uint32_t)lo, (uint32_t)hi);
            got_card = ucpset_cardinality(buf, n);
            ASSERT(intervals_equal(buf, n, exp, exp_count),
                   "exhaustive inplace complement xm=%u lo=%zu hi=%zu", xm, lo, hi);
            ASSERT(got_card == exp_card,
                   "exhaustive inplace complement card xm=%u lo=%zu hi=%zu", xm, lo, hi);
          }
        }
      }

      // predicates
      {
        // precedes / succeeds
        {
          bool exp_precedes, exp_succeeds;
          if (xcount == 0)
            exp_precedes = (ycount != 0);
          else if (ycount == 0)
            exp_precedes = false;
          else
            exp_precedes = (xr[2 * xcount - 1] <= yr[0]);

          if (xcount == 0)
            exp_succeeds = false;
          else if (ycount == 0)
            exp_succeeds = true;
          else
            exp_succeeds = (xr[0] >= yr[2 * ycount - 1]);

          ASSERT(ucpset_precedes(xr, xcount, yr, ycount) == exp_precedes,
                 "exhaustive precedes xm=%u ym=%u", xm, ym);
          ASSERT(ucpset_succeeds(xr, xcount, yr, ycount) == exp_succeeds,
                 "exhaustive succeeds xm=%u ym=%u", xm, ym);
        }

        // equal
        {
          bool exp_equal = (xm == ym);
          ASSERT(ucpset_equal(xr, xcount, yr, ycount) == exp_equal,
                 "exhaustive equal xm=%u ym=%u", xm, ym);
        }

        // subset
        {
          bool exp_subset = ((xm & ym) == xm);
          ASSERT(ucpset_subset(xr, xcount, yr, ycount) == exp_subset,
                 "exhaustive subset xm=%u ym=%u", xm, ym);
        }

        // superset
        {
          bool exp_superset = ((ym & xm) == ym);
          ASSERT(ucpset_superset(xr, xcount, yr, ycount) == exp_superset,
                 "exhaustive superset xm=%u ym=%u", xm, ym);
        }

        // disjoint
        {
          bool exp_disjoint = ((xm & ym) == 0);
          ASSERT(ucpset_disjoint(xr, xcount, yr, ycount) == exp_disjoint,
                 "exhaustive disjoint xm=%u ym=%u", xm, ym);
        }
      }
    }

    // insert element (only outer loop)
    for (uint32_t v = 0; v < N; v++) {
      bv_clear(&btmp);
      for (size_t i = 0; i < N; i++)
        btmp.bits[i] = bx.bits[i];
      btmp.bits[v] = 1;

      uint32_t exp[2*N];
      size_t exp_count = bv_to_intervals(&btmp, exp);

      uint32_t buf[2*N + 2];
      memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
      size_t n = ucpset_insert(buf, xcount, v);
      ASSERT(intervals_equal(buf, n, exp, exp_count),
             "exhaustive insert xm=%u v=%u", xm, v);
      validate_intervals(buf, n, "exhaustive insert");
    }

    // insert interval
    for (uint32_t lo = 0; lo < N; lo++) {
      for (uint32_t hi = lo + 1; hi <= N; hi++) {
        bv_clear(&btmp);
        for (size_t i = 0; i < N; i++) {
          if (i >= lo && i < hi)
            btmp.bits[i] = 1;
          else
            btmp.bits[i] = bx.bits[i];
        }

        uint32_t exp[2*N];
        size_t exp_count = bv_to_intervals(&btmp, exp);

        uint32_t buf[2*N + 2];
        memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
        size_t n = ucpset_insert_interval(buf, xcount, lo, hi);
        ASSERT(intervals_equal(buf, n, exp, exp_count),
               "exhaustive insert_iv xm=%u lo=%u hi=%u", xm, lo, hi);
        validate_intervals(buf, n, "exhaustive insert_iv");
      }
    }

    // remove element
    for (uint32_t v = 0; v < N; v++) {
      bv_clear(&btmp);
      for (size_t i = 0; i < N; i++)
        btmp.bits[i] = bx.bits[i];
      btmp.bits[v] = 0;

      uint32_t exp[2*N];
      size_t exp_count = bv_to_intervals(&btmp, exp);

      uint32_t buf[2*N + 2];
      memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
      size_t n = ucpset_remove(buf, xcount, v);
      ASSERT(intervals_equal(buf, n, exp, exp_count),
             "exhaustive remove xm=%u v=%u", xm, v);
      validate_intervals(buf, n, "exhaustive remove");
    }

    // remove interval
    for (uint32_t lo = 0; lo < N; lo++) {
      for (uint32_t hi = lo + 1; hi <= N; hi++) {
        bv_clear(&btmp);
        for (size_t i = 0; i < N; i++) {
          if (i >= lo && i < hi)
            btmp.bits[i] = 0;
          else
            btmp.bits[i] = bx.bits[i];
        }

        uint32_t exp[2*N];
        size_t exp_count = bv_to_intervals(&btmp, exp);

        uint32_t buf[2*N + 2];
        memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
        size_t n = ucpset_remove_interval(buf, xcount, lo, hi);
        ASSERT(intervals_equal(buf, n, exp, exp_count),
               "exhaustive remove_iv xm=%u lo=%u hi=%u", xm, lo, hi);
        validate_intervals(buf, n, "exhaustive remove_iv");
      }
    }

    // contains element
    for (uint32_t v = 0; v < N; v++) {
      bool exp = bx.bits[v] ? true : false;
      bool got = ucpset_contains(xr, xcount, v);
      ASSERT(got == exp, "exhaustive contains xm=%u v=%u", xm, v);
    }

    // contains interval
    for (uint32_t lo = 0; lo < N; lo++) {
      for (uint32_t hi = lo + 1; hi <= N; hi++) {
        bool exp = true;
        for (size_t i = lo; i < hi; i++) {
          if (!bx.bits[i]) { exp = false; break; }
        }
        bool got = ucpset_contains_interval(xr, xcount, lo, hi);
        ASSERT(got == exp, "exhaustive contains_iv xm=%u lo=%u hi=%u", xm, lo, hi);
      }
    }
  }

  bv_free(&bx);
  bv_free(&by);
  bv_free(&btmp);
}

// =============================== Fuzz tests ================================

static void test_fuzz(void) {
  const size_t N = 256;
  const int ITERS = 1000;

  bitvec_t bx = bv_make(N);
  bitvec_t by = bv_make(N);
  bitvec_t btmp = bv_make(N);

  uint32_t xr[N * 2], yr[N * 2], result[N * 2], expected[N * 2];
  uint32_t inplace_buf[N * 4];

  for (int iter = 0; iter < ITERS; iter++) {
    size_t xcount = random_set(xr, N);
    size_t ycount = random_set(yr, N);

    bv_from_intervals(&bx, xr, xcount);
    bv_from_intervals(&by, yr, ycount);

    // out-of-place union
    {
      bv_union(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      size_t got_count = ucpset_union(result, xr, xcount, yr, ycount);
      ASSERT(intervals_equal(result, got_count, expected, exp_count),
             "fuzz union iter=%d", iter);
      validate_intervals(result, got_count, "fuzz union");
    }

    // out-of-place intersection
    {
      bv_intersection(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      size_t got_count = ucpset_intersection(result, xr, xcount, yr, ycount);
      ASSERT(intervals_equal(result, got_count, expected, exp_count),
             "fuzz intersection iter=%d", iter);
      validate_intervals(result, got_count, "fuzz intersection");

      uint32_t exp_card = bv_cardinality(&btmp);
      ASSERT(ucpset_cardinality_intersection(xr, xcount, yr, ycount) == exp_card,
             "fuzz intersection cardinality iter=%d", iter);
    }

    // out-of-place difference
    {
      bv_difference(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      size_t got_count = ucpset_difference(result, xr, xcount, yr, ycount);
      ASSERT(intervals_equal(result, got_count, expected, exp_count),
             "fuzz difference iter=%d", iter);
      validate_intervals(result, got_count, "fuzz difference");
    }

    // out-of-place xor
    {
      bv_xor(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      size_t got_count = ucpset_xor(result, xr, xcount, yr, ycount);
      ASSERT(intervals_equal(result, got_count, expected, exp_count),
             "fuzz xor iter=%d", iter);
      validate_intervals(result, got_count, "fuzz xor");
    }

    // complement for random universe slice
    {
      uint32_t lo = rng_bounded((uint32_t)N);
      uint32_t hi = lo + 1 + rng_bounded((uint32_t)(N - lo));

      bv_complement(&btmp, &bx, lo, hi);

      uint32_t exp[2*N];
      size_t exp_count = bv_to_intervals(&btmp, exp);
      uint32_t exp_card = bv_cardinality(&btmp);

      // out-of-place
      {
        size_t n = ucpset_complement(result, xr, xcount, lo, hi);
        ASSERT(intervals_equal(result, n, exp, exp_count),
               "fuzz complement iter=%d", iter);
        validate_intervals(result, n, "fuzz complement");
      }

      // in-place complement
      {
        uint32_t got_card;
        memcpy(inplace_buf, xr, xcount * 2 * sizeof(uint32_t));
        size_t n = ucpset_inplace_complement(inplace_buf, xcount, lo, hi);
        got_card = ucpset_cardinality(inplace_buf, n);
        ASSERT(intervals_equal(inplace_buf, n, exp, exp_count),
               "fuzz inplace complement iter=%d", iter);
        ASSERT(got_card == exp_card,
               "fuzz inplace complement card iter=%d", iter);
      }
    }

    // in-place union
    {
      bv_union(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      uint32_t exp_card = bv_cardinality(&btmp);
      uint32_t got_card;
      memcpy(inplace_buf, xr, xcount * 2 * sizeof(uint32_t));
      size_t got = ucpset_inplace_union(inplace_buf, xcount, yr, ycount);
      got_card = ucpset_cardinality(inplace_buf, got);
      ASSERT(intervals_equal(inplace_buf, got, expected, exp_count),
             "fuzz inplace union iter=%d", iter);
      validate_intervals(inplace_buf, got, "fuzz inplace union");
      ASSERT(got_card == exp_card,
             "fuzz inplace union card iter=%d", iter);
    }

    // in-place intersection
    {
      bv_intersection(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      uint32_t exp_card = bv_cardinality(&btmp);
      uint32_t got_card;
      memcpy(inplace_buf, xr, xcount * 2 * sizeof(uint32_t));
      size_t got = ucpset_inplace_intersection(inplace_buf, xcount, yr, ycount);
      got_card = ucpset_cardinality(inplace_buf, got);
      ASSERT(intervals_equal(inplace_buf, got, expected, exp_count),
             "fuzz inplace intersection iter=%d", iter);
      ASSERT(got_card == exp_card,
             "fuzz inplace intersection card iter=%d", iter);
    }

    // in-place difference
    {
      bv_difference(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      uint32_t exp_card = bv_cardinality(&btmp);
      uint32_t got_card;
      memcpy(inplace_buf, xr, xcount * 2 * sizeof(uint32_t));
      size_t got = ucpset_inplace_difference(inplace_buf, xcount, yr, ycount);
      got_card = ucpset_cardinality(inplace_buf, got);
      ASSERT(intervals_equal(inplace_buf, got, expected, exp_count),
             "fuzz inplace difference iter=%d", iter);
      ASSERT(got_card == exp_card,
             "fuzz inplace difference card iter=%d", iter);
    }

    // in-place xor
    {
      bv_xor(&btmp, &bx, &by);
      size_t exp_count = bv_to_intervals(&btmp, expected);
      uint32_t exp_card = bv_cardinality(&btmp);
      uint32_t got_card;
      memcpy(inplace_buf, xr, xcount * 2 * sizeof(uint32_t));
      size_t got = ucpset_inplace_xor(inplace_buf, xcount, yr, ycount);
      got_card = ucpset_cardinality(inplace_buf, got);
      ASSERT(intervals_equal(inplace_buf, got, expected, exp_count),
             "fuzz inplace xor iter=%d", iter);
      ASSERT(got_card == exp_card,
             "fuzz inplace xor card iter=%d", iter);
    }

    // membership: contains element
    for (uint32_t v = 0; v < (uint32_t)N; v++) {
      bool exp = bx.bits[v];
      ASSERT(ucpset_contains(xr, xcount, v) == exp,
             "fuzz contains v=%u iter=%d", v, iter);
    }

    // membership: contains interval
    for (size_t lo = 0; lo < N; lo++) {
      for (size_t hi = lo + 1; hi <= N; hi++) {
        bool exp = true;
        for (size_t i = lo; i < hi; i++) {
          if (!bx.bits[i]) { exp = false; break; }
        }
        ASSERT(ucpset_contains_interval(xr, xcount, (uint32_t)lo, (uint32_t)hi) == exp,
               "fuzz contains_iv lo=%zu hi=%zu iter=%d", lo, hi, iter);
      }
    }

    // insert element (random)
    {
      uint32_t v = rng_bounded((uint32_t)N);
      uint32_t singleton[] = {v, v + 1};

      uint32_t ibuf[N * 2 + 2];
      memcpy(ibuf, xr, xcount * 2 * sizeof(uint32_t));
      size_t in = ucpset_insert(ibuf, xcount, v);

      uint32_t ubuf[N * 2 + 2];
      size_t un = ucpset_union(ubuf, xr, xcount, singleton, 1);

      ASSERT(intervals_equal(ibuf, in, ubuf, un),
             "fuzz insert==union v=%u iter=%d", v, iter);
    }

    // insert element boundary stress
    {
      uint32_t vals[] = {0, 1, 2, (uint32_t)(N-2), (uint32_t)(N-1),
                         (uint32_t)N, (uint32_t)(N+1), (uint32_t)(N+2)};
      for (size_t vi = 0; vi < sizeof(vals)/sizeof(vals[0]); vi++) {
        uint32_t v = vals[vi];
        uint32_t singleton[] = {v, v + 1};

        uint32_t ibuf[N * 2 + 2];
        memcpy(ibuf, xr, xcount * 2 * sizeof(uint32_t));
        size_t in = ucpset_insert(ibuf, xcount, v);

        uint32_t ubuf[N * 2 + 2];
        size_t un = ucpset_union(ubuf, xr, xcount, singleton, 1);

        ASSERT(intervals_equal(ibuf, in, ubuf, un),
               "fuzz insert boundary v=%u iter=%d", v, iter);
      }
    }

    // insert interval (random)
    {
      uint32_t lo = rng_bounded((uint32_t)N);
      uint32_t hi = lo + 1 + rng_bounded((uint32_t)(N - lo));
      uint32_t interval[] = {lo, hi};

      uint32_t ibuf[N * 2 + 2];
      memcpy(ibuf, xr, xcount * 2 * sizeof(uint32_t));
      size_t in = ucpset_insert_interval(ibuf, xcount, lo, hi);

      uint32_t ubuf[N * 2 + 2];
      size_t un = ucpset_union(ubuf, xr, xcount, interval, 1);

      ASSERT(intervals_equal(ibuf, in, ubuf, un),
             "fuzz insert_iv==union lo=%u hi=%u iter=%d", lo, hi, iter);
    }

    // insert interval boundary stress
    {
      uint32_t vals[] = {0, 1, 2, (uint32_t)(N-2), (uint32_t)(N-1),
                         (uint32_t)N, (uint32_t)(N+1), (uint32_t)(N+2)};
      for (size_t vi = 0; vi < sizeof(vals)/sizeof(vals[0]); vi++) {
        uint32_t lo = vals[vi];
        uint32_t hi = lo + 1;
        uint32_t singleton[] = {lo, hi};

        uint32_t ibuf[N * 2 + 2];
        memcpy(ibuf, xr, xcount * 2 * sizeof(uint32_t));
        size_t in = ucpset_insert_interval(ibuf, xcount, lo, hi);

        uint32_t ubuf[N * 2 + 2];
        size_t un = ucpset_union(ubuf, xr, xcount, singleton, 1);

        ASSERT(intervals_equal(ibuf, in, ubuf, un),
               "fuzz insert_iv boundary lo=%u iter=%d", lo, iter);
      }
    }

    // remove element (random)
    {
      uint32_t v = rng_bounded((uint32_t)N);
      uint32_t singleton[] = {v, v + 1};

      uint32_t rbuf[N * 2 + 2];
      memcpy(rbuf, xr, xcount * 2 * sizeof(uint32_t));
      size_t rn = ucpset_remove(rbuf, xcount, v);

      uint32_t dbuf[N * 2 + 2];
      size_t dn = ucpset_difference(dbuf, xr, xcount, singleton, 1);

      ASSERT(intervals_equal(rbuf, rn, dbuf, dn),
             "fuzz remove==diff v=%u iter=%d", v, iter);
    }

    // remove element boundary stress
    {
      uint32_t vals[] = {0, 1, 2, (uint32_t)(N-2), (uint32_t)(N-1),
                         (uint32_t)N, (uint32_t)(N+1), (uint32_t)(N+2)};
      for (size_t vi = 0; vi < sizeof(vals)/sizeof(vals[0]); vi++) {
        uint32_t v = vals[vi];
        uint32_t singleton[] = {v, v + 1};

        uint32_t rbuf[N * 2 + 2];
        memcpy(rbuf, xr, xcount * 2 * sizeof(uint32_t));
        size_t rn = ucpset_remove(rbuf, xcount, v);

        uint32_t dbuf[N * 2 + 2];
        size_t dn = ucpset_difference(dbuf, xr, xcount, singleton, 1);

        ASSERT(intervals_equal(rbuf, rn, dbuf, dn),
               "fuzz remove boundary v=%u iter=%d", v, iter);
      }
    }

    // remove interval (random)
    {
      uint32_t lo = rng_bounded((uint32_t)N);
      uint32_t hi = lo + 1 + rng_bounded((uint32_t)(N - lo));
      uint32_t interval[] = {lo, hi};

      uint32_t rbuf[N * 2 + 2];
      memcpy(rbuf, xr, xcount * 2 * sizeof(uint32_t));
      size_t rn = ucpset_remove_interval(rbuf, xcount, lo, hi);

      uint32_t dbuf[N * 2 + 2];
      size_t dn = ucpset_difference(dbuf, xr, xcount, interval, 1);

      ASSERT(intervals_equal(rbuf, rn, dbuf, dn),
             "fuzz remove_iv==diff lo=%u hi=%u iter=%d", lo, hi, iter);
    }

    // remove interval boundary stress
    {
      uint32_t vals[] = {0, 1, 2, (uint32_t)(N-2), (uint32_t)(N-1),
                         (uint32_t)N, (uint32_t)(N+1), (uint32_t)(N+2)};
      for (size_t vi = 0; vi < sizeof(vals)/sizeof(vals[0]); vi++) {
        uint32_t lo = vals[vi];
        uint32_t hi = lo + 1;
        uint32_t singleton[] = {lo, hi};

        uint32_t rbuf[N * 2 + 2];
        memcpy(rbuf, xr, xcount * 2 * sizeof(uint32_t));
        size_t rn = ucpset_remove_interval(rbuf, xcount, lo, hi);

        uint32_t dbuf[N * 2 + 2];
        size_t dn = ucpset_difference(dbuf, xr, xcount, singleton, 1);

        ASSERT(intervals_equal(rbuf, rn, dbuf, dn),
               "fuzz remove_iv boundary lo=%u iter=%d", lo, iter);
      }
    }

    // predicates via bitmap
    {
      // subset
      {
        bool exp_subset = true;
        for (size_t i = 0; i < N; i++) {
          if (bx.bits[i] && !by.bits[i]) exp_subset = false;
        }
        ASSERT(ucpset_subset(xr, xcount, yr, ycount) == exp_subset,
               "fuzz subset iter=%d", iter);
      }

      // superset
      {
        bool exp_superset = true;
        for (size_t i = 0; i < N; i++) {
          if (!bx.bits[i] && by.bits[i]) exp_superset = false;
        }
        ASSERT(ucpset_superset(xr, xcount, yr, ycount) == exp_superset,
               "fuzz superset iter=%d", iter);
      }

      // disjoint
      {
        bool exp_disjoint = true;
        for (size_t i = 0; i < N; i++)
          if (bx.bits[i] && by.bits[i]) { exp_disjoint = false; break; }
        ASSERT(ucpset_disjoint(xr, xcount, yr, ycount) == exp_disjoint,
               "fuzz disjoint iter=%d", iter);
      }

      // equal
      {
        bool exp_equal = true;
        for (size_t i = 0; i < N; i++)
          if (bx.bits[i] != by.bits[i]) { exp_equal = false; break; }
        ASSERT(ucpset_equal(xr, xcount, yr, ycount) == exp_equal,
               "fuzz equal iter=%d", iter);
      }
    }
  }

  bv_free(&bx);
  bv_free(&by);
  bv_free(&btmp);
}

// ========================= Search tests (coverage) =========================

static void test_search(void) {
  // empty set
  ASSERT(ucpset_upper_bound_hi(NULL, 0, 5) == 0, "upper_bound: empty");
  ASSERT(ucpset_lower_bound_hi(NULL, 0, 5) == 0, "lower_bound: empty");

  // single interval [10, 20)
  {
    uint32_t x[] = {10, 20};
    ASSERT(ucpset_upper_bound_hi(x, 1, 5) == 0, "upper_bound: before");
    ASSERT(ucpset_upper_bound_hi(x, 1, 20) == 1, "upper_bound: at hi");
    ASSERT(ucpset_upper_bound_hi(x, 1, 25) == 1, "upper_bound: after");
    ASSERT(ucpset_upper_bound_hi(x, 1, 15) == 0, "upper_bound: inside");

    ASSERT(ucpset_lower_bound_hi(x, 1, 5) == 0, "lower_bound: before");
    ASSERT(ucpset_lower_bound_hi(x, 1, 20) == 0, "lower_bound: at hi");
    ASSERT(ucpset_lower_bound_hi(x, 1, 21) == 1, "lower_bound: after");
  }

  // 5 intervals to exercise the binary search loop
  {
    uint32_t x[] = {0, 10, 20, 30, 40, 50, 60, 70, 80, 90};
    ASSERT(ucpset_upper_bound_hi(x, 5, 5) == 0, "upper_bound 5ivs: 5");
    ASSERT(ucpset_upper_bound_hi(x, 5, 10) == 1, "upper_bound 5ivs: 10");
    ASSERT(ucpset_upper_bound_hi(x, 5, 25) == 1, "upper_bound 5ivs: 25");
    ASSERT(ucpset_upper_bound_hi(x, 5, 30) == 2, "upper_bound 5ivs: 30");
    ASSERT(ucpset_upper_bound_hi(x, 5, 45) == 2, "upper_bound 5ivs: 45");
    ASSERT(ucpset_upper_bound_hi(x, 5, 50) == 3, "upper_bound 5ivs: 50");
    ASSERT(ucpset_upper_bound_hi(x, 5, 89) == 4, "upper_bound 5ivs: 89");
    ASSERT(ucpset_upper_bound_hi(x, 5, 90) == 5, "upper_bound 5ivs: 90");
    ASSERT(ucpset_upper_bound_hi(x, 5, 100) == 5, "upper_bound 5ivs: 100");

    ASSERT(ucpset_lower_bound_hi(x, 5, 5) == 0, "lower_bound 5ivs: 5");
    ASSERT(ucpset_lower_bound_hi(x, 5, 10) == 0, "lower_bound 5ivs: 10");
    ASSERT(ucpset_lower_bound_hi(x, 5, 11) == 1, "lower_bound 5ivs: 11");
    ASSERT(ucpset_lower_bound_hi(x, 5, 30) == 1, "lower_bound 5ivs: 30");
    ASSERT(ucpset_lower_bound_hi(x, 5, 31) == 2, "lower_bound 5ivs: 31");
    ASSERT(ucpset_lower_bound_hi(x, 5, 50) == 2, "lower_bound 5ivs: 50");
    ASSERT(ucpset_lower_bound_hi(x, 5, 70) == 3, "lower_bound 5ivs: 70");
    ASSERT(ucpset_lower_bound_hi(x, 5, 90) == 4, "lower_bound 5ivs: 90");
    ASSERT(ucpset_lower_bound_hi(x, 5, 91) == 5, "lower_bound 5ivs: 91");
  }

  // 8 intervals for deeper loop coverage
  {
    uint32_t x[] = {0,2, 4,6, 8,10, 12,14, 16,18, 20,22, 24,26, 28,30};
    for (uint32_t key = 0; key <= 31; key++) {
      size_t ub = ucpset_upper_bound_hi(x, 8, key);
      for (size_t j = 0; j < ub; j++)
        ASSERT(x[2*j+1] <= key, "upper_bound verify j=%zu key=%u", j, key);
      for (size_t j = ub; j < 8; j++)
        ASSERT(x[2*j+1] > key, "upper_bound verify j=%zu key=%u", j, key);

      size_t lb = ucpset_lower_bound_hi(x, 8, key);
      for (size_t j = 0; j < lb; j++)
        ASSERT(x[2*j+1] < key, "lower_bound verify j=%zu key=%u", j, key);
      for (size_t j = lb; j < 8; j++)
        ASSERT(x[2*j+1] >= key, "lower_bound verify j=%zu key=%u", j, key);
    }
  }
}

// =================== Inplace fast-path tests (coverage) ====================

static void test_inplace_fastpaths(void) {
  // --- inplace_union fast paths ---

  // xcount == 0: copy y into x
  {
    uint32_t buf[32] = {0};
    uint32_t y[] = {10, 20, 30, 40};
    size_t n = ucpset_inplace_union(buf, 0, y, 2);
    expect_eq("inplace_union: x=empty", buf, n, y, 2);
  }

  // x_hi < y_lo: disjoint, x before y → append
  {
    uint32_t buf[32] = {10, 20};
    uint32_t y[] = {30, 40};
    size_t n = ucpset_inplace_union(buf, 1, y, 1);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("inplace_union: x<y append", buf, n, exp, 2);
  }

  // x_hi == y_lo: touching, x before y → merge
  {
    uint32_t buf[32] = {10, 20};
    uint32_t y[] = {20, 30};
    size_t n = ucpset_inplace_union(buf, 1, y, 1);
    uint32_t exp[] = {10, 30};
    expect_eq("inplace_union: x touches y", buf, n, exp, 1);
  }

  // x_hi == y_lo, ycount > 1: touching with more y intervals
  {
    uint32_t buf[32] = {10, 20};
    uint32_t y[] = {20, 30, 40, 50};
    size_t n = ucpset_inplace_union(buf, 1, y, 2);
    uint32_t exp[] = {10, 30, 40, 50};
    expect_eq("inplace_union: x touches y multi", buf, n, exp, 2);
  }

  // y_hi < x_lo: disjoint, y before x → prepend
  {
    uint32_t buf[32] = {30, 40};
    uint32_t y[] = {10, 20};
    size_t n = ucpset_inplace_union(buf, 1, y, 1);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("inplace_union: y<x prepend", buf, n, exp, 2);
  }

  // y_hi == x_lo: touching, y before x → merge
  {
    uint32_t buf[32] = {20, 30};
    uint32_t y[] = {10, 20};
    size_t n = ucpset_inplace_union(buf, 1, y, 1);
    uint32_t exp[] = {10, 30};
    expect_eq("inplace_union: y touches x", buf, n, exp, 1);
  }

  // y_hi == x_lo, xcount > 1: touching with more x intervals
  {
    uint32_t buf[32] = {20, 30, 40, 50};
    uint32_t y[] = {10, 20};
    size_t n = ucpset_inplace_union(buf, 2, y, 1);
    uint32_t exp[] = {10, 30, 40, 50};
    expect_eq("inplace_union: y touches x multi", buf, n, exp, 2);
  }

  // self-alias: should no-op
  {
    uint32_t buf[32] = {10, 20};
    size_t n = ucpset_inplace_union(buf, 1, buf, 1);
    uint32_t exp[] = {10, 20};
    expect_eq("inplace_union: self", buf, n, exp, 1);
  }

  // --- inplace_xor fast paths ---

  // xcount == 0: copy y
  {
    uint32_t buf[32] = {0};
    uint32_t y[] = {10, 20, 30, 40};
    size_t n = ucpset_inplace_xor(buf, 0, y, 2);
    expect_eq("inplace_xor: x=empty", buf, n, y, 2);
  }

  // both empty
  {
    uint32_t buf[4] = {0};
    size_t n = ucpset_inplace_xor(buf, 0, buf, 0);
    ASSERT(n == 0, "inplace_xor: both empty");
  }

  // x_hi < y_lo: disjoint, x before y → append
  {
    uint32_t buf[32] = {10, 20};
    uint32_t y[] = {30, 40};
    size_t n = ucpset_inplace_xor(buf, 1, y, 1);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("inplace_xor: x<y append", buf, n, exp, 2);
  }

  // x_hi == y_lo: touching → merge for xor
  {
    uint32_t buf[32] = {10, 20};
    uint32_t y[] = {20, 30};
    size_t n = ucpset_inplace_xor(buf, 1, y, 1);
    uint32_t exp[] = {10, 30};
    expect_eq("inplace_xor: x touches y", buf, n, exp, 1);
  }

  // x_hi == y_lo, ycount > 1
  {
    uint32_t buf[32] = {10, 20};
    uint32_t y[] = {20, 30, 40, 50};
    size_t n = ucpset_inplace_xor(buf, 1, y, 2);
    uint32_t exp[] = {10, 30, 40, 50};
    expect_eq("inplace_xor: x touches y multi", buf, n, exp, 2);
  }

  // y_hi < x_lo: y before x → prepend
  {
    uint32_t buf[32] = {30, 40};
    uint32_t y[] = {10, 20};
    size_t n = ucpset_inplace_xor(buf, 1, y, 1);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("inplace_xor: y<x prepend", buf, n, exp, 2);
  }

  // y_hi == x_lo: touching → merge
  {
    uint32_t buf[32] = {20, 30};
    uint32_t y[] = {10, 20};
    size_t n = ucpset_inplace_xor(buf, 1, y, 1);
    uint32_t exp[] = {10, 30};
    expect_eq("inplace_xor: y touches x", buf, n, exp, 1);
  }

  // y_hi == x_lo, xcount > 1
  {
    uint32_t buf[32] = {20, 30, 40, 50};
    uint32_t y[] = {10, 20};
    size_t n = ucpset_inplace_xor(buf, 2, y, 1);
    uint32_t exp[] = {10, 30, 40, 50};
    expect_eq("inplace_xor: y touches x multi", buf, n, exp, 2);
  }

  // self-alias: should give empty (A xor A = empty)
  {
    uint32_t buf[32] = {10, 20};
    size_t n = ucpset_inplace_xor(buf, 1, buf, 1);
    ASSERT(n == 0, "inplace_xor: self");
  }

  // --- inplace_intersection self-alias ---
  {
    uint32_t buf[32] = {10, 20};
    size_t n = ucpset_inplace_intersection(buf, 1, buf, 1);
    uint32_t exp[] = {10, 20};
    expect_eq("inplace_inter: self", buf, n, exp, 1);
  }

  // --- inplace_difference self-alias ---
  {
    uint32_t buf[32] = {10, 20};
    size_t n = ucpset_inplace_difference(buf, 1, buf, 1);
    ASSERT(n == 0, "inplace_diff: self");
  }
}

// ============ Merge fast paths: result and its cardinality ================

static void test_merge_fastpaths_cardinality(void) {
  uint32_t dst[32];
  uint32_t card;
  size_t n;

  uint32_t a[] = {10, 20};   // cardinality 10
  uint32_t b[] = {30, 40};   // cardinality 10
  uint32_t c[] = {20, 30};   // touches a

  // --- union ---

  // empty x, non-empty y
  {
    n = ucpset_union(dst, NULL, 0, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 10, "union_card: empty+A");
  }

  // empty x, empty y
  {
    n = ucpset_union(dst, NULL, 0, NULL, 0);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 0 && card == 0, "union_card: empty+empty");
  }

  // non-empty x, empty y
  {
    n = ucpset_union(dst, a, 1, NULL, 0);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 10, "union_card: A+empty");
  }

  // x_hi < y_lo (disjoint)
  {
    n = ucpset_union(dst, a, 1, b, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 2 && card == 20, "union_card: disjoint");
  }

  // x_hi == y_lo (touching)
  {
    n = ucpset_union(dst, a, 1, c, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 20, "union_card: touching");
  }

  // y_hi < x_lo (disjoint reversed)
  {
    n = ucpset_union(dst, b, 1, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 2 && card == 20, "union_card: disjoint rev");
  }

  // y_hi == x_lo (touching reversed)
  {
    n = ucpset_union(dst, c, 1, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 20, "union_card: touching rev");
  }

  // intersection
  {
    n = ucpset_intersection(dst, NULL, 0, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 0 && card == 0, "inter_card: empty");
  }
  {
    n = ucpset_intersection(dst, a, 1, b, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 0 && card == 0, "inter_card: disjoint");
  }

  // difference
  {
    n = ucpset_difference(dst, NULL, 0, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 0 && card == 0, "diff_card: empty x");
  }
  {
    n = ucpset_difference(dst, a, 1, NULL, 0);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 10, "diff_card: empty y");
  }
  {
    n = ucpset_difference(dst, a, 1, b, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 10, "diff_card: disjoint");
  }

  // xor
  {
    n = ucpset_xor(dst, NULL, 0, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 10, "xor_card: empty x");
  }
  {
    n = ucpset_xor(dst, NULL, 0, NULL, 0);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 0 && card == 0, "xor_card: both empty");
  }
  {
    n = ucpset_xor(dst, a, 1, NULL, 0);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 10, "xor_card: empty y");
  }
  {
    n = ucpset_xor(dst, a, 1, b, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 2 && card == 20, "xor_card: disjoint");
  }
  {
    n = ucpset_xor(dst, a, 1, c, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 20, "xor_card: touching");
  }
  {
    n = ucpset_xor(dst, b, 1, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 2 && card == 20, "xor_card: disjoint rev");
  }
  {
    n = ucpset_xor(dst, c, 1, a, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 1 && card == 20, "xor_card: touching rev");
  }

  // complement
  {
    n = ucpset_complement(dst, a, 1, 0, 30);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 2 && card == 20, "complement_card: basic");
  }
  {
    n = ucpset_complement(dst, NULL, 0, 5, 5);
    card = ucpset_cardinality(dst, n);
    ASSERT(n == 0 && card == 0, "complement_card: empty range");
  }

  // sweep path
  {
    uint32_t e[] = {5, 25};  // overlaps a
    n = ucpset_union(dst, a, 1, e, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(card == 20, "union_card: overlap");
  }
  {
    uint32_t e[] = {5, 25};  // overlaps a
    n = ucpset_xor(dst, a, 1, e, 1);
    card = ucpset_cardinality(dst, n);
    ASSERT(card == 10, "xor_card: overlap");  // [5,10) + [20,25) = 10
  }
}

// =========== Inplace fast paths: result and its cardinality ===============

static void test_inplace_fastpaths_cardinality(void) {
  uint32_t card;
  size_t n;

  uint32_t a[] = {10, 20};   // cardinality 10
  uint32_t b[] = {30, 40};   // cardinality 10
  uint32_t c[] = {20, 30};   // touches a

  // --- inplace_union ---

  // self-alias
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_union(buf, 1, buf, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_union_card: self");
  }

  // ycount == 0
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_union(buf, 1, NULL, 0);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_union_card: empty y");
  }

  // xcount == 0
  {
    uint32_t buf[32] = {0};
    n = ucpset_inplace_union(buf, 0, a, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_union_card: empty x");
  }

  // disjoint x < y
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_union(buf, 1, b, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 2 && card == 20, "ip_union_card: disjoint");
  }

  // touching x < y
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_union(buf, 1, c, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 20, "ip_union_card: touching");
  }

  // disjoint y < x
  {
    uint32_t buf[32] = {30, 40};
    n = ucpset_inplace_union(buf, 1, a, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 2 && card == 20, "ip_union_card: disjoint rev");
  }

  // touching y < x
  {
    uint32_t buf[32] = {20, 30};
    n = ucpset_inplace_union(buf, 1, a, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 20, "ip_union_card: touching rev");
  }

  // overlap (sweep path)
  {
    uint32_t buf[32] = {10, 20};
    uint32_t e[] = {5, 25};
    n = ucpset_inplace_union(buf, 1, e, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(card == 20, "ip_union_card: overlap");
  }

  // --- inplace_intersection ---

  // self-alias
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_intersection(buf, 1, buf, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_inter_card: self");
  }

  // empty
  {
    n = ucpset_inplace_intersection(NULL, 0, a, 1);
    card = ucpset_cardinality(NULL, n);
    ASSERT(n == 0 && card == 0, "ip_inter_card: empty");
  }

  // disjoint
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_intersection(buf, 1, b, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 0 && card == 0, "ip_inter_card: disjoint");
  }

  // --- inplace_difference ---

  // self-alias
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_difference(buf, 1, buf, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 0 && card == 0, "ip_diff_card: self");
  }

  // xcount == 0
  {
    n = ucpset_inplace_difference(NULL, 0, a, 1);
    card = ucpset_cardinality(NULL, n);
    ASSERT(n == 0 && card == 0, "ip_diff_card: empty x");
  }

  // ycount == 0
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_difference(buf, 1, NULL, 0);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_diff_card: empty y");
  }

  // disjoint
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_difference(buf, 1, b, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_diff_card: disjoint");
  }

  // --- inplace_xor ---

  // self-alias
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_xor(buf, 1, buf, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 0 && card == 0, "ip_xor_card: self");
  }

  // xcount == 0, ycount > 0
  {
    uint32_t buf[32] = {0};
    n = ucpset_inplace_xor(buf, 0, a, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_xor_card: empty x");
  }

  // xcount == 0, ycount == 0
  {
    uint32_t buf[4] = {0};
    n = ucpset_inplace_xor(buf, 0, buf, 0);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 0 && card == 0, "ip_xor_card: both empty");
  }

  // ycount == 0
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_xor(buf, 1, NULL, 0);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 10, "ip_xor_card: empty y");
  }

  // disjoint
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_xor(buf, 1, b, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 2 && card == 20, "ip_xor_card: disjoint");
  }

  // touching
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_xor(buf, 1, c, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 20, "ip_xor_card: touching");
  }

  // disjoint reversed
  {
    uint32_t buf[32] = {30, 40};
    n = ucpset_inplace_xor(buf, 1, a, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 2 && card == 20, "ip_xor_card: disjoint rev");
  }

  // touching reversed
  {
    uint32_t buf[32] = {20, 30};
    n = ucpset_inplace_xor(buf, 1, a, 1);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 1 && card == 20, "ip_xor_card: touching rev");
  }

  // --- inplace_complement ---

  // basic
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_complement(buf, 1, 0, 30);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 2 && card == 20, "ip_comp_card: basic");
  }

  // empty universe
  {
    uint32_t buf[32] = {10, 20};
    n = ucpset_inplace_complement(buf, 1, 5, 5);
    card = ucpset_cardinality(buf, n);
    ASSERT(n == 0 && card == 0, "ip_comp_card: empty universe");
  }
}

// ================== Membership edge case tests (coverage) ==================

static void test_membership_edge(void) {
  // contains: empty set
  ASSERT(!ucpset_contains(NULL, 0, 5), "contains: empty");

  // contains: value past end of last interval
  {
    uint32_t x[] = {10, 20};
    ASSERT(!ucpset_contains(x, 1, 50), "contains: past end");
    ASSERT(!ucpset_contains(x, 1, 20), "contains: at hi");
  }

  // contains_interval: empty set
  ASSERT(!ucpset_contains_interval(NULL, 0, 5, 10), "contains_iv: empty");

  // contains_interval: lo >= hi (empty query) is vacuously true,
  // wherever it lies and even for the empty set, matching ucpset_subset
  {
    uint32_t x[] = {10, 20};
    ASSERT(ucpset_contains_interval(x, 1, 20, 10), "contains_iv: reversed");
    ASSERT(ucpset_contains_interval(x, 1, 15, 15), "contains_iv: empty inside");
    ASSERT(ucpset_contains_interval(x, 1, 5, 5), "contains_iv: empty before");
    ASSERT(ucpset_contains_interval(x, 1, 30, 30), "contains_iv: empty after");
    ASSERT(ucpset_contains_interval(NULL, 0, 5, 5), "contains_iv: empty in empty set");
    ASSERT(ucpset_contains_interval(NULL, 0, 10, 5), "contains_iv: reversed in empty set");
    ASSERT(ucpset_subset(NULL, 0, x, 1) && ucpset_subset(NULL, 0, NULL, 0),
           "contains_iv: agrees with subset of the empty set");
  }

  // contains_interval: queries against multi-interval set
  {
    uint32_t x[] = {0, 5, 10, 20};
    ASSERT(ucpset_contains_interval(x, 2, 10, 15), "contains_iv: last iv");
    ASSERT(ucpset_contains_interval(x, 2, 10, 20), "contains_iv: exact last");
    ASSERT(!ucpset_contains_interval(x, 2, 10, 21), "contains_iv: past last");
    ASSERT(!ucpset_contains_interval(x, 2, 8, 15), "contains_iv: span last lo");
    ASSERT(ucpset_contains_interval(x, 2, 1, 3), "contains_iv: first iv");
    ASSERT(!ucpset_contains_interval(x, 2, 3, 7), "contains_iv: cross gap");
  }
}

// ================== contains with a hint ==================

static void test_contains_hint(void) {
  // Empty set: false, whatever the hint holds; NULL is allowed.
  {
    size_t hint = 12345;
    ASSERT(!ucpset_contains_hint(NULL, 0, 5, &hint), "hint: empty set");
  }

  // Every set over a 10-codepoint universe, every codepoint, every starting
  // hint including ones past the end: same answer as ucpset_contains, and
  // the hint comes back as an interval index.
  {
    enum { U = 10 };
    for (uint32_t mask = 0; mask < (1u << U); mask++) {
      uint32_t x[U + 2];
      size_t xcount = 0;
      for (uint32_t c = 0; c < U; c++) {
        if (!(mask >> c & 1))
          continue;
        if (xcount > 0 && x[2 * xcount - 1] == c)
          x[2 * xcount - 1] = c + 1;
        else {
          x[2 * xcount] = c;
          x[2 * xcount + 1] = c + 1;
          xcount++;
        }
      }
      static const size_t wild[] = {SIZE_MAX, SIZE_MAX / 2, 1000};
      for (uint32_t v = 0; v < U + 2; v++) {
        const bool exp = ucpset_contains(x, xcount, v);
        for (size_t h = 0; h < xcount + 3 + 3; h++) {
          size_t hint = h < xcount + 3 ? h : wild[h - (xcount + 3)];
          const size_t before = hint;
          bool got = ucpset_contains_hint(x, xcount, v, &hint);
          ASSERT(got == exp, "hint: mask=%u v=%u hint=%zu", mask, v, before);
          ASSERT(hint <= xcount || hint == before,
                 "hint: out of range mask=%u v=%u hint=%zu -> %zu", mask, v, before, hint);
          // A second lookup of the same codepoint from the returned hint.
          ASSERT(ucpset_contains_hint(x, xcount, v, &hint) == exp,
                 "hint: repeat mask=%u v=%u hint=%zu", mask, v, before);
        }
      }
    }
  }

  // Long sets in exact-size buffers, one hint carried through a stream of
  // lookups that mixes short steps with jumps of every length in both
  // directions, so the forward and backward searches run at every distance.
  for (int iter = 0; iter < 300; iter++) {
    const size_t xcount = 1 + rng_bounded(iter < 100 ? 8 : 700);
    uint32_t* x = malloc(2 * xcount * sizeof(uint32_t));
    uint32_t p = rng_bounded(3);
    for (size_t t = 0; t < xcount; t++) {
      p += rng_bounded(4) == 0 ? 0 : 1 + rng_bounded(5);
      if (t > 0 && p == x[2 * t - 1])
        p++;
      x[2 * t] = p;
      p += 1 + rng_bounded(4);
      x[2 * t + 1] = p;
    }
    const uint32_t top = x[2 * xcount - 1] + 4;

    size_t hint = rng_bounded(2) ? 0 : rng_next();
    uint32_t v = rng_bounded(top);
    for (int k = 0; k < 3000; k++) {
      switch (rng_bounded(6)) {
        case 0: v = rng_bounded(top); break;                           // anywhere
        case 1: v = v + 1 + rng_bounded(1u << rng_bounded(12)); break; // forward jump
        case 2: {                                                      // backward jump
          uint32_t d = 1 + rng_bounded(1u << rng_bounded(12));
          v = v > d ? v - d : 0;
          break;
        }
        case 3: v = rng_bounded(x[1] + 2); break;                      // around the first interval
        default: v = v + rng_bounded(3); break;                        // short step
      }
      if (v >= top)
        v = top - 1;
      const size_t before = hint;
      bool got = ucpset_contains_hint(x, xcount, v, &hint);
      ASSERT(got == ucpset_contains(x, xcount, v),
             "hint stream: iter=%d k=%d v=%u hint=%zu", iter, k, v, before);
      ASSERT(hint <= xcount || hint == before, "hint stream: out of range iter=%d k=%d", iter, k);
      if (rng_bounded(500) == 0)
        hint = rng_next();  // a stale hint
    }
    free(x);
  }
}

// ================== Insert interval edge cases (coverage) ==================

static void test_insert_interval_edge(void) {
  // insert interval with no overlap, into middle
  {
    uint32_t buf[20] = {10, 20, 40, 50};
    size_t n = ucpset_insert_interval(buf, 2, 25, 35);
    uint32_t exp[] = {10, 20, 25, 35, 40, 50};
    expect_eq("insert_iv: no overlap middle", buf, n, exp, 3);
  }

  // insert interval at end, no overlap
  {
    uint32_t buf[20] = {10, 20};
    size_t n = ucpset_insert_interval(buf, 1, 30, 40);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("insert_iv: no overlap end", buf, n, exp, 2);
  }

  // insert interval at beginning, no overlap
  {
    uint32_t buf[20] = {30, 40};
    size_t n = ucpset_insert_interval(buf, 1, 10, 20);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("insert_iv: no overlap begin", buf, n, exp, 2);
  }

  // insert interval that merges middle intervals with trailing
  {
    uint32_t buf[20] = {0, 5, 10, 15, 20, 25, 30, 35, 50, 60};
    size_t n = ucpset_insert_interval(buf, 5, 8, 32);
    uint32_t exp[] = {0, 5, 8, 35, 50, 60};
    expect_eq("insert_iv: merge middle with tail", buf, n, exp, 3);
  }

  // hi above the Unicode bound is clamped (fast path: appended at end)
  {
    uint32_t buf[20] = {10, 20};
    size_t n = ucpset_insert_interval(buf, 1, 0x10FFF0, 0x200000);
    uint32_t exp[] = {10, 20, 0x10FFF0, UCPSET_BOUND_MAX};
    expect_eq("insert_iv: clamp hi, append", buf, n, exp, 2);
    ASSERT(ucpset_canonical(buf, n), "insert_iv: clamp hi, append canonical");
  }

  // hi above the Unicode bound is clamped (merges everything from lo up)
  {
    uint32_t buf[20] = {10, 20, 40, 50, 0x10FFFF, UCPSET_BOUND_MAX};
    size_t n = ucpset_insert_interval(buf, 3, 15, UINT32_MAX);
    uint32_t exp[] = {10, UCPSET_BOUND_MAX};
    expect_eq("insert_iv: clamp hi, merge", buf, n, exp, 1);
    ASSERT(ucpset_canonical(buf, n), "insert_iv: clamp hi, merge canonical");
  }

  // hi above the Unicode bound, into an empty set
  {
    uint32_t buf[20];
    size_t n = ucpset_insert_interval(buf, 0, 5, UCPSET_BOUND_MAX + 1);
    uint32_t exp[] = {5, UCPSET_BOUND_MAX};
    expect_eq("insert_iv: clamp hi, empty set", buf, n, exp, 1);
  }

  // interval entirely above the Unicode bound is a no-op
  {
    uint32_t buf[20] = {10, 20};
    size_t n = ucpset_insert_interval(buf, 1, UCPSET_BOUND_MAX, 0x200000);
    uint32_t exp[] = {10, 20};
    expect_eq("insert_iv: above bound no-op", buf, n, exp, 1);
    n = ucpset_insert_interval(buf, 1, 0x150000, 0x200000);
    expect_eq("insert_iv: far above bound no-op", buf, n, exp, 1);
  }

  // complement family clamps the universe's hi the same way
  {
    uint32_t x[] = {10, 20};
    uint32_t exp[] = {0, 10, 20, UCPSET_BOUND_MAX};
    uint32_t card = 0;

    uint32_t dst[20];
    size_t n = ucpset_complement(dst, x, 1, 0, UINT32_MAX);
    card = ucpset_cardinality(dst, n);
    expect_eq("complement: clamp hi", dst, n, exp, 2);
    ASSERT(ucpset_canonical(dst, n), "complement: clamp hi canonical");
    ASSERT(card == UCPSET_BOUND_MAX - 10, "complement: clamp hi cardinality");

    uint32_t buf[20] = {10, 20};
    card = 0;
    n = ucpset_inplace_complement(buf, 1, 0, UINT32_MAX);
    card = ucpset_cardinality(buf, n);
    expect_eq("inplace_complement: clamp hi", buf, n, exp, 2);
    ASSERT(card == UCPSET_BOUND_MAX - 10, "inplace_complement: clamp hi cardinality");

    // universe entirely above the bound is empty
    ASSERT(ucpset_complement(dst, x, 1, UCPSET_BOUND_MAX, UINT32_MAX) == 0,
           "complement: universe above bound empty");
  }
}

// ================== Remove interval edge cases (coverage) ==================

static void test_remove_interval_edge(void) {
  // remove interval that doesn't overlap any existing interval
  {
    uint32_t buf[20] = {10, 20, 30, 40};
    size_t n = ucpset_remove_interval(buf, 2, 22, 28);
    uint32_t exp[] = {10, 20, 30, 40};
    expect_eq("remove_iv: no overlap", buf, n, exp, 2);
  }

  // remove interval entirely after all intervals
  {
    uint32_t buf[] = {10, 20, 0, 0};
    size_t n = ucpset_remove_interval(buf, 1, 50, 60);
    uint32_t exp[] = {10, 20};
    expect_eq("remove_iv: after all", buf, n, exp, 1);
  }

  // remove interval entirely before all intervals
  {
    uint32_t buf[] = {30, 40, 0, 0};
    size_t n = ucpset_remove_interval(buf, 1, 10, 20);
    uint32_t exp[] = {30, 40};
    expect_eq("remove_iv: before all", buf, n, exp, 1);
  }

  // remove with empty interval (no-op)
  {
    uint32_t buf[] = {10, 20};
    size_t n = ucpset_remove_interval(buf, 1, 15, 15);
    ASSERT(n == 1, "remove_iv: empty range noop");
  }

  // remove from empty set
  {
    uint32_t buf[4];
    size_t n = ucpset_remove_interval(buf, 0, 10, 20);
    ASSERT(n == 0, "remove_iv: empty set");
  }

  // multiple non-overlapping with removal in between
  {
    uint32_t buf[20] = {5, 10, 20, 25, 35, 40};
    size_t n = ucpset_remove_interval(buf, 3, 12, 37);
    uint32_t exp[] = {5, 10, 37, 40};
    expect_eq("remove_iv: partial overlap multi", buf, n, exp, 2);
  }
}

// ==================== Cardinality edge cases (coverage) ====================

static void test_cardinality_edge(void) {
  uint32_t a[] = {10, 20};
  uint32_t b[] = {30, 40};
  uint32_t c[] = {15, 25};
  uint32_t d[] = {20, 30};  // touches a and b

  // empty operands
  ASSERT(ucpset_cardinality_intersection(NULL, 0, a, 1) == 0, "cardinality_intersection: empty x");
  ASSERT(ucpset_cardinality_intersection(a, 1, NULL, 0) == 0, "cardinality_intersection: empty y");
  ASSERT(ucpset_cardinality_intersection(NULL, 0, NULL, 0) == 0, "cardinality_intersection: both empty");

  // one set entirely before the other, with a gap and touching
  ASSERT(ucpset_cardinality_intersection(a, 1, b, 1) == 0, "cardinality_intersection: apart");
  ASSERT(ucpset_cardinality_intersection(b, 1, a, 1) == 0, "cardinality_intersection: apart rev");
  ASSERT(ucpset_cardinality_intersection(a, 1, d, 1) == 0, "cardinality_intersection: touching");
  ASSERT(ucpset_cardinality_intersection(d, 1, a, 1) == 0, "cardinality_intersection: touching rev");

  // overlap, containment, identity
  ASSERT(ucpset_cardinality_intersection(a, 1, c, 1) == 5, "cardinality_intersection: overlap");
  ASSERT(ucpset_cardinality_intersection(c, 1, a, 1) == 5, "cardinality_intersection: overlap rev");
  ASSERT(ucpset_cardinality_intersection(a, 1, a, 1) == 10, "cardinality_intersection: self");
  {
    uint32_t wide[] = {0, 100};
    uint32_t many[] = {10, 20, 30, 40, 50, 60};
    ASSERT(ucpset_cardinality_intersection(many, 3, wide, 1) == 30, "cardinality_intersection: inside wide");
    ASSERT(ucpset_cardinality_intersection(wide, 1, many, 3) == 30, "cardinality_intersection: wide over many");
  }

  // the other operations follow from it
  {
    uint32_t r[8];
    uint32_t ca = ucpset_cardinality(a, 1), cc = ucpset_cardinality(c, 1);
    uint32_t ci = ucpset_cardinality_intersection(a, 1, c, 1);
    ASSERT(ucpset_cardinality(r, ucpset_union(r, a, 1, c, 1)) == ca + cc - ci, "identity: union");
    ASSERT(ucpset_cardinality(r, ucpset_difference(r, a, 1, c, 1)) == ca - ci, "identity: difference");
    ASSERT(ucpset_cardinality(r, ucpset_xor(r, a, 1, c, 1)) == ca + cc - 2 * ci, "identity: xor");
  }
}

// ===================== Predicate edge cases (coverage) =====================

static void test_predicate_edge(void) {
  // succeeds with empty
  {
    uint32_t a[] = {5, 25};
    ASSERT(ucpset_succeeds(a, 1, NULL, 0), "succeeds: A > empty");
  }

  // subset with empty superset
  {
    uint32_t a[] = {5, 25};
    ASSERT(!ucpset_subset(a, 1, NULL, 0), "subset: A not sub empty");
  }

  // superset of empty
  {
    uint32_t a[] = {5, 25};
    ASSERT(ucpset_superset(a, 1, NULL, 0), "superset: A sup empty");
  }
}

// ====================== Strict canonical output tests ======================

static void test_strict_canonical_output(void) {
  uint32_t result[32];

  // union of touching intervals from different sets must coalesce
  {
    uint32_t a[] = {0, 5};
    uint32_t b[] = {5, 10};
    size_t n = ucpset_union(result, a, 1, b, 1);
    validate_intervals(result, n, "strict: union touching");
    uint32_t exp[] = {0, 10};
    expect_eq("strict: union touching", result, n, exp, 1);
  }

  // xor of touching intervals
  {
    uint32_t a[] = {0, 10};
    uint32_t b[] = {5, 15};
    size_t n = ucpset_xor(result, a, 1, b, 1);
    validate_intervals(result, n, "strict: xor");
    uint32_t exp[] = {0, 5, 10, 15};
    expect_eq("strict: xor adjacent", result, n, exp, 2);
  }

  // complement must be non-adjacent
  {
    uint32_t a[] = {5, 10};
    size_t n = ucpset_complement(result, a, 1, 0, 20);
    validate_intervals(result, n, "strict: complement");
    uint32_t exp[] = {0, 5, 10, 20};
    expect_eq("strict: complement", result, n, exp, 2);
  }

  // in-place union of touching
  {
    uint32_t buf[8] = {0, 5};
    uint32_t b[] = {5, 10};
    size_t n = ucpset_inplace_union(buf, 1, b, 1);
    validate_intervals(buf, n, "strict: inplace union touching");
    uint32_t exp[] = {0, 10};
    expect_eq("strict: inplace union touching", buf, n, exp, 1);
  }

  // insert that creates non-adjacent result
  {
    uint32_t buf[12] = {0, 5, 10, 15};
    size_t n = ucpset_insert(buf, 2, 7);
    validate_intervals(buf, n, "strict: insert gap");
    uint32_t exp[] = {0, 5, 7, 8, 10, 15};
    expect_eq("strict: insert gap", buf, n, exp, 3);
  }
}

// ===================== Single-interval operations ==========================

/*
 * Every set over an 8-codepoint universe against every [lo, hi), empty and
 * reversed ones included.  Each _interval operation is run out of place and
 * with dst == x, in buffers of exactly the documented capacity, and checked
 * against the bitvec oracle.  The run lookups are checked against a scan.
 */
static void test_interval_ops(void) {
  enum { IN = 8 };
  static const char* const names[] = {"union", "intersection", "difference", "xor"};
  bitvec_t bx = bv_make(IN), br = bv_make(IN), bt = bv_make(IN);

  for (unsigned xm = 0; xm < (1u << IN); xm++) {
    uint32_t xr[2 * IN], exp[2 * IN];
    for (unsigned p = 0; p < IN; p++)
      bx.bits[p] = (xm >> p) & 1;
    size_t xcount = bv_to_intervals(&bx, xr);

    for (uint32_t lo = 0; lo <= IN; lo++) {
      for (uint32_t hi = 0; hi <= IN; hi++) {
        bv_clear(&br);
        for (uint32_t p = lo; p < hi; p++)
          br.bits[p] = 1;

        // run lookups
        {
          size_t i, j, first = xcount, end = xcount;
          ucpset_find_overlap_interval(xr, xcount, lo, hi, &i, &j);
          for (size_t t = 0; t < xcount; t++) {
            if (lo < hi && xr[2 * t] < hi && xr[2 * t + 1] > lo) {
              if (first == xcount) first = t;
              end = t + 1;
            }
          }
          if (first == xcount) {
            ASSERT(i == j, "find_overlap: none xm=%u lo=%u hi=%u", xm, lo, hi);
          } else {
            ASSERT(i == first && j == end, "find_overlap xm=%u lo=%u hi=%u", xm, lo, hi);
          }

          ucpset_find_coalesce_interval(xr, xcount, lo, hi, &i, &j);
          first = end = xcount;
          size_t before = 0;   // intervals ending strictly before lo
          for (size_t t = 0; t < xcount; t++) {
            if (xr[2 * t + 1] < lo) before++;
            if (lo < hi && xr[2 * t] <= hi && xr[2 * t + 1] >= lo) {
              if (first == xcount) first = t;
              end = t + 1;
            }
          }
          if (first == xcount) {
            ASSERT(i == j, "find_coalesce: none xm=%u lo=%u hi=%u", xm, lo, hi);
            if (lo < hi)
              ASSERT(i == before, "find_coalesce: insert position xm=%u lo=%u hi=%u", xm, lo, hi);
          } else {
            ASSERT(i == first && j == end, "find_coalesce xm=%u lo=%u hi=%u", xm, lo, hi);
          }
        }

        for (int op = 0; op < 4; op++) {
          if (op == 0) bv_union(&bt, &bx, &br);
          else if (op == 1) bv_intersection(&bt, &bx, &br);
          else if (op == 2) bv_difference(&bt, &bx, &br);
          else bv_xor(&bt, &bx, &br);
          size_t exp_count = bv_to_intervals(&bt, exp);

          // documented capacity: xcount for intersection, xcount + 1 otherwise
          size_t cap = xcount + (op != 1);
          for (int inplace = 0; inplace < 2; inplace++) {
            uint32_t* dst = (uint32_t*)malloc((cap ? cap : 1) * 2 * sizeof(uint32_t));
            ASSERT(dst, "OOM");
            const uint32_t* src = xr;
            if (inplace) {
              if (xcount)
                memcpy(dst, xr, xcount * 2 * sizeof(uint32_t));
              src = dst;
            }

            size_t n;
            if (op == 0) n = ucpset_union_interval(dst, src, xcount, lo, hi);
            else if (op == 1) n = ucpset_intersection_interval(dst, src, xcount, lo, hi);
            else if (op == 2) n = ucpset_difference_interval(dst, src, xcount, lo, hi);
            else n = ucpset_xor_interval(dst, src, xcount, lo, hi);

            ASSERT(intervals_equal(dst, n, exp, exp_count),
                   "%s_interval%s xm=%u lo=%u hi=%u", names[op],
                   inplace ? " (dst == x)" : "", xm, lo, hi);
            validate_intervals(dst, n, "interval op");
            free(dst);
          }
        }
      }
    }
  }

  // union and xor clamp hi to the Unicode bound; the others need no clamp
  {
    uint32_t x[] = {10, 20};
    uint32_t dst[8];
    size_t n = ucpset_union_interval(dst, x, 1, 15, UINT32_MAX);
    uint32_t exp_u[] = {10, UCPSET_BOUND_MAX};
    expect_eq("union_interval: clamp hi", dst, n, exp_u, 1);

    n = ucpset_xor_interval(dst, x, 1, 15, UINT32_MAX);
    uint32_t exp_x[] = {10, 15, 20, UCPSET_BOUND_MAX};
    expect_eq("xor_interval: clamp hi", dst, n, exp_x, 2);

    n = ucpset_union_interval(dst, x, 1, UCPSET_BOUND_MAX, UINT32_MAX);
    uint32_t exp_same[] = {10, 20};
    expect_eq("union_interval: above bound no-op", dst, n, exp_same, 1);

    n = ucpset_intersection_interval(dst, x, 1, 15, UINT32_MAX);
    uint32_t exp_i[] = {15, 20};
    expect_eq("intersection_interval: huge hi", dst, n, exp_i, 1);

    n = ucpset_difference_interval(dst, x, 1, 15, UINT32_MAX);
    uint32_t exp_d[] = {10, 15};
    expect_eq("difference_interval: huge hi", dst, n, exp_d, 1);

    // empty set
    n = ucpset_union_interval(dst, NULL, 0, 3, 7);
    uint32_t exp_e[] = {3, 7};
    expect_eq("union_interval: empty set", dst, n, exp_e, 1);
    n = ucpset_xor_interval(dst, NULL, 0, 3, 7);
    expect_eq("xor_interval: empty set", dst, n, exp_e, 1);
    ASSERT(ucpset_intersection_interval(dst, NULL, 0, 3, 7) == 0, "intersection_interval: empty set");
    ASSERT(ucpset_difference_interval(dst, NULL, 0, 3, 7) == 0, "difference_interval: empty set");
  }

  bv_free(&bx);
  bv_free(&br);
  bv_free(&bt);
}

// ============================ Editing by position ==========================

/*
 * The block helpers (ucpset_copy, ucpset_move, ucpset_open_run,
 * ucpset_open_run_to), ucpset_find, ucpset_slice and ucpset_splice.  Buffers
 * are allocated at exactly the documented capacity.
 */
static void test_editing(void) {
  enum { EN = 8 };

  // ucpset_move: overlapping in both directions, and nothing at all
  {
    uint32_t a[] = {1, 2, 3, 4, 5, 6, 0, 0};
    ASSERT(ucpset_move(a + 2, a, 3) == 3, "move: returns n");
    uint32_t up[] = {1, 2, 1, 2, 3, 4, 5, 6};
    ASSERT(memcmp(a, up, sizeof(up)) == 0, "move: up by one interval");

    ASSERT(ucpset_move(a, a + 2, 3) == 3, "move: returns n");
    uint32_t down[] = {1, 2, 3, 4, 5, 6, 5, 6};
    ASSERT(memcmp(a, down, sizeof(down)) == 0, "move: down by one interval");

    ASSERT(ucpset_move(a, a, 3) == 3, "move: onto itself");
    ASSERT(memcmp(a, down, sizeof(down)) == 0, "move: onto itself unchanged");
    ASSERT(ucpset_move(NULL, NULL, 0) == 0, "move: zero intervals");
  }

  // ucpset_copy
  {
    uint32_t src[] = {1, 2, 3, 4, 5, 6};
    uint32_t dst[6] = {0};
    ASSERT(ucpset_copy(dst, src, 3) == 3, "copy: returns n");
    ASSERT(memcmp(dst, src, sizeof(src)) == 0, "copy: three intervals");
    ASSERT(ucpset_copy(NULL, NULL, 0) == 0, "copy: zero intervals");
  }

  // ucpset_open_run and ucpset_open_run_to: every run of a short array
  // replaced by 0..3 free slots.  The slots themselves are unspecified, so
  // only the parts before and after them are compared.
  for (size_t n = 0; n <= 5; n++) {
    for (size_t i = 0; i <= n; i++) {
      for (size_t j = i; j <= n; j++) {
        for (size_t k = 0; k <= 3; k++) {
          size_t out = n - (j - i) + k;
          size_t cap = n > out ? n : out;
          uint32_t src[2 * 5 + 1];
          for (size_t w = 0; w < 2 * n; w++)
            src[w] = 100 + (uint32_t)w;

          for (int to = 0; to < 2; to++) {
            uint32_t* buf = (uint32_t*)malloc((cap ? cap : 1) * 2 * sizeof(uint32_t));
            ASSERT(buf, "OOM");
            size_t got;
            if (to) {
              got = ucpset_open_run_to(buf, src, n, i, j, k);
            } else {
              if (n)
                memcpy(buf, src, 2 * n * sizeof(uint32_t));
              got = ucpset_open_run(buf, n, i, j, k);
            }
            ASSERT(got == out, "open_run%s: count n=%zu i=%zu j=%zu k=%zu",
                   to ? "_to" : "", n, i, j, k);
            ASSERT(i == 0 || memcmp(buf, src, 2 * i * sizeof(uint32_t)) == 0,
                   "open_run%s: head n=%zu i=%zu j=%zu k=%zu", to ? "_to" : "", n, i, j, k);
            ASSERT(j == n || memcmp(buf + 2 * (i + k), src + 2 * j,
                                    2 * (n - j) * sizeof(uint32_t)) == 0,
                   "open_run%s: tail n=%zu i=%zu j=%zu k=%zu", to ? "_to" : "", n, i, j, k);
            free(buf);
          }

          // ucpset_open_run_to with dst == x is ucpset_open_run
          uint32_t* buf = (uint32_t*)malloc((cap ? cap : 1) * 2 * sizeof(uint32_t));
          ASSERT(buf, "OOM");
          if (n)
            memcpy(buf, src, 2 * n * sizeof(uint32_t));
          ASSERT(ucpset_open_run_to(buf, buf, n, i, j, k) == out, "open_run_to in place: count");
          ASSERT(i == 0 || memcmp(buf, src, 2 * i * sizeof(uint32_t)) == 0,
                 "open_run_to in place: head");
          ASSERT(j == n || memcmp(buf + 2 * (i + k), src + 2 * j,
                                  2 * (n - j) * sizeof(uint32_t)) == 0,
                 "open_run_to in place: tail");
          free(buf);
        }
      }
    }
  }

  bitvec_t bx = bv_make(EN);
  for (unsigned xm = 0; xm < (1u << EN); xm++) {
    uint32_t xr[2 * EN];
    for (unsigned p = 0; p < EN; p++)
      bx.bits[p] = (xm >> p) & 1;
    size_t xcount = bv_to_intervals(&bx, xr);

    // ucpset_find against a scan, and against ucpset_contains
    for (uint32_t v = 0; v <= EN + 1; v++) {
      size_t exp = xcount;
      for (size_t t = 0; t < xcount; t++)
        if (xr[2 * t] <= v && v < xr[2 * t + 1])
          exp = t;
      size_t got = ucpset_find(xr, xcount, v);
      ASSERT(got == exp, "find xm=%u v=%u", xm, v);
      ASSERT((got < xcount) == ucpset_contains(xr, xcount, v),
             "find agrees with contains xm=%u v=%u", xm, v);
    }

    // ucpset_slice: every run, out of place and overlapping its source
    for (size_t i = 0; i <= xcount; i++) {
      for (size_t j = i; j <= xcount; j++) {
        size_t k = j - i;

        uint32_t* dst = (uint32_t*)malloc((k ? k : 1) * 2 * sizeof(uint32_t));
        ASSERT(dst, "OOM");
        ASSERT(ucpset_slice(dst, xr, i, j) == k, "slice: count xm=%u", xm);
        ASSERT(intervals_equal(dst, k, xr + 2 * i, k), "slice xm=%u i=%zu j=%zu", xm, i, j);
        validate_intervals(dst, k, "slice");
        free(dst);

        // onto the start of its own buffer
        uint32_t* buf = (uint32_t*)malloc((xcount ? xcount : 1) * 2 * sizeof(uint32_t));
        ASSERT(buf, "OOM");
        if (xcount)
          memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
        ASSERT(ucpset_slice(buf, buf, i, j) == k, "slice in place: count xm=%u", xm);
        ASSERT(intervals_equal(buf, k, xr + 2 * i, k),
               "slice in place xm=%u i=%zu j=%zu", xm, i, j);
        free(buf);
      }
    }

    // find + splice reproduce insert_interval and remove_interval
    for (uint32_t lo = 0; lo < EN; lo++) {
      for (uint32_t hi = lo + 1; hi <= EN; hi++) {
        size_t i, j;
        uint32_t* buf = (uint32_t*)malloc((xcount + 1) * 2 * sizeof(uint32_t));
        uint32_t* ref = (uint32_t*)malloc((xcount + 1) * 2 * sizeof(uint32_t));
        ASSERT(buf && ref, "OOM");

        // union: the coalescing run becomes one interval
        if (xcount) {
          memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
          memcpy(ref, xr, xcount * 2 * sizeof(uint32_t));
        }
        ucpset_find_coalesce_interval(buf, xcount, lo, hi, &i, &j);
        uint32_t one[2] = {lo, hi};
        if (i < j) {
          one[0] = ucpset_min(lo, buf[2 * i]);
          one[1] = ucpset_max(hi, buf[2 * j - 1]);
        }
        size_t n = ucpset_splice(buf, xcount, i, j, one, 1);
        size_t rn = ucpset_insert_interval(ref, xcount, lo, hi);
        ASSERT(intervals_equal(buf, n, ref, rn),
               "splice as insert xm=%u lo=%u hi=%u", xm, lo, hi);
        validate_intervals(buf, n, "splice as insert");

        // difference: the overlapping run leaves at most a piece at each end
        if (xcount) {
          memcpy(buf, xr, xcount * 2 * sizeof(uint32_t));
          memcpy(ref, xr, xcount * 2 * sizeof(uint32_t));
        }
        ucpset_find_overlap_interval(buf, xcount, lo, hi, &i, &j);
        uint32_t ends[4];
        size_t ecount = 0;
        if (i < j) {
          uint32_t first_lo = buf[2 * i], last_hi = buf[2 * j - 1];
          if (first_lo < lo) {
            ends[2 * ecount] = first_lo;
            ends[2 * ecount + 1] = lo;
            ecount++;
          }
          if (last_hi > hi) {
            ends[2 * ecount] = hi;
            ends[2 * ecount + 1] = last_hi;
            ecount++;
          }
        }
        n = ucpset_splice(buf, xcount, i, j, ends, ecount);
        rn = ucpset_remove_interval(ref, xcount, lo, hi);
        ASSERT(intervals_equal(buf, n, ref, rn),
               "splice as remove xm=%u lo=%u hi=%u", xm, lo, hi);
        validate_intervals(buf, n, "splice as remove");

        free(buf);
        free(ref);
      }
    }
  }
  bv_free(&bx);

  // ucpset_splice mechanics: every run of a short array replaced by 0..3
  // intervals, checked word for word
  for (size_t n = 0; n <= 5; n++) {
    for (size_t i = 0; i <= n; i++) {
      for (size_t j = i; j <= n; j++) {
        for (size_t yc = 0; yc <= 3; yc++) {
          size_t out = n - (j - i) + yc;
          size_t cap = n > out ? n : out;
          uint32_t* buf = (uint32_t*)malloc((cap ? cap : 1) * 2 * sizeof(uint32_t));
          ASSERT(buf, "OOM");
          uint32_t y[6], exp[2 * 8];
          for (size_t t = 0; t < 2 * n; t++)
            buf[t] = 100 + (uint32_t)t;
          for (size_t t = 0; t < 2 * yc; t++)
            y[t] = 900 + (uint32_t)t;

          size_t k = 0;
          for (size_t t = 0; t < 2 * i; t++) exp[k++] = 100 + (uint32_t)t;
          for (size_t t = 0; t < 2 * yc; t++) exp[k++] = 900 + (uint32_t)t;
          for (size_t t = 2 * j; t < 2 * n; t++) exp[k++] = 100 + (uint32_t)t;

          size_t got = ucpset_splice(buf, n, i, j, yc ? y : NULL, yc);
          ASSERT(got == out, "splice: count n=%zu i=%zu j=%zu yc=%zu", n, i, j, yc);
          ASSERT(got == 0 || memcmp(buf, exp, got * 2 * sizeof(uint32_t)) == 0,
                 "splice n=%zu i=%zu j=%zu yc=%zu", n, i, j, yc);
          free(buf);
        }
      }
    }
  }
}

// ============================ Pairwise disjoint ============================

// The reference: every pair, with ucpset_disjoint.
static bool every_pair_disjoint(const uint32_t* const* sets, const size_t* counts,
                                size_t k) {
  for (size_t a = 0; a < k; a++)
    for (size_t b = a + 1; b < k; b++)
      if (!ucpset_disjoint(sets[a], counts[a], sets[b], counts[b]))
        return false;
  return true;
}

static void test_pairwise_disjoint(void) {
  enum { U = 6, ALL = 1 << U };
  static uint32_t set[ALL][2 * U];
  static size_t count[ALL];

  // every set over a 6-codepoint universe; the bitmask is the oracle
  for (unsigned m = 0; m < ALL; m++) {
    size_t n = 0;
    for (uint32_t v = 0; v < U; v++) {
      if (!((m >> v) & 1))
        continue;
      if (n > 0 && set[m][2 * n - 1] == v) {
        set[m][2 * n - 1] = v + 1;
      } else {
        set[m][2 * n] = v;
        set[m][2 * n + 1] = v + 1;
        n++;
      }
    }
    count[m] = n;
  }

  // every pair and every triple
  for (unsigned a = 0; a < ALL; a++) {
    for (unsigned b = 0; b < ALL; b++) {
      const uint32_t* s2[2] = {set[a], set[b]};
      size_t n2[2] = {count[a], count[b]};
      size_t scratch[6];
      ASSERT(ucpset_pairwise_disjoint(s2, n2, 2, scratch) == ((a & b) == 0),
             "pairwise_disjoint pair a=%u b=%u", a, b);

      for (unsigned c = 0; c < ALL; c++) {
        const uint32_t* s3[3] = {set[a], set[b], set[c]};
        size_t n3[3] = {count[a], count[b], count[c]};
        bool exp = (a & b) == 0 && (a & c) == 0 && (b & c) == 0;
        ASSERT(ucpset_pairwise_disjoint(s3, n3, 3, scratch) == exp,
               "pairwise_disjoint triple a=%u b=%u c=%u", a, b, c);
      }
    }
  }

  // no sets, one set, empty sets given as NULL, the same set twice
  {
    size_t scratch[8];
    const uint32_t* some[3] = {NULL, set[5], NULL};
    size_t some_n[3] = {0, count[5], 0};
    const uint32_t* twice[3] = {set[5], set[2], set[5]};
    size_t twice_n[3] = {count[5], count[2], count[5]};
    const uint32_t* empties[3] = {NULL, NULL, NULL};
    size_t empties_n[3] = {0, 0, 0};

    ASSERT(ucpset_pairwise_disjoint(NULL, NULL, 0, scratch), "pairwise_disjoint: no sets");
    ASSERT(ucpset_pairwise_disjoint(some + 1, some_n + 1, 1, scratch), "pairwise_disjoint: one set");
    ASSERT(ucpset_pairwise_disjoint(some, some_n, 3, scratch), "pairwise_disjoint: empty sets");
    ASSERT(ucpset_pairwise_disjoint(empties, empties_n, 3, scratch), "pairwise_disjoint: all empty");
    ASSERT(!ucpset_pairwise_disjoint(twice, twice_n, 3, scratch), "pairwise_disjoint: same set twice");
  }

  // 3 to 12 random sets against every pair, with the scratch array at
  // exactly 2 * k entries
  {
    enum { KMAX = 12, IMAX = 30 };
    static uint32_t buf[KMAX][2 * IMAX];
    uint64_t r = 88172645463325252ull;
    long disjoint = 0;

    for (int it = 0; it < 200000; it++) {
      r ^= r << 13; r ^= r >> 7; r ^= r << 17;
      size_t k = 3 + (size_t)(r % (KMAX - 2));
      uint32_t span = 40 + (uint32_t)((r >> 8) % 4000);
      const uint32_t* sets[KMAX];
      size_t counts[KMAX];

      for (size_t t = 0; t < k; t++) {
        size_t c = 0;
        uint32_t p = 0;
        for (;;) {
          r ^= r << 13; r ^= r >> 7; r ^= r << 17;
          p += 1 + (uint32_t)(r % (span / 3 + 1));
          uint32_t hi = p + 1 + (uint32_t)((r >> 20) % 4);
          if (hi > span || c == IMAX)
            break;
          buf[t][2 * c] = p;
          buf[t][2 * c + 1] = hi;
          c++;
          p = hi;
        }
        sets[t] = buf[t];
        counts[t] = c;
      }

      size_t* scratch = (size_t*)malloc(2 * k * sizeof(size_t));
      ASSERT(scratch, "OOM");
      bool exp = every_pair_disjoint(sets, counts, k);
      ASSERT(ucpset_pairwise_disjoint(sets, counts, k, scratch) == exp,
             "pairwise_disjoint random it=%d k=%zu", it, k);
      free(scratch);
      disjoint += exp;
    }
    ASSERT(disjoint > 1000, "pairwise_disjoint random: too few disjoint cases to mean much");
  }

  // sets that come in runs, so that skipping gallops: a canonical set dealt
  // out to five sets in runs of 1..12 intervals is disjoint; moving one
  // interval's end past the start of the next breaks that
  {
    enum { K = 5, TOTAL = 400 };
    static uint32_t part[K][2 * TOTAL];
    size_t counts[K] = {0};
    const uint32_t* sets[K];
    size_t scratch[2 * K];
    uint32_t p = 0;
    size_t owner = 0, left = 0;

    for (size_t i = 0; i < TOTAL; i++) {
      if (left == 0) {
        owner = (owner + 1 + rng_bounded(K - 1)) % K;
        left = 1 + rng_bounded(12);
      }
      left--;
      p += 2 + rng_bounded(5);
      part[owner][2 * counts[owner]] = p;
      p += 1 + rng_bounded(5);
      part[owner][2 * counts[owner] + 1] = p;
      counts[owner]++;
    }
    for (size_t t = 0; t < K; t++)
      sets[t] = part[t];
    ASSERT(ucpset_pairwise_disjoint(sets, counts, K, scratch), "pairwise_disjoint: runs");

    for (size_t t = 0; t < K; t++) {
      for (size_t i = 0; i + 1 < counts[t]; i += 7) {
        uint32_t saved = part[t][2 * i + 1];
        // reach to just short of this set's next interval: stays canonical,
        // and overlaps another set exactly when one lies in between
        part[t][2 * i + 1] = part[t][2 * i + 2] - 1;
        ASSERT(ucpset_pairwise_disjoint(sets, counts, K, scratch) ==
               every_pair_disjoint(sets, counts, K),
               "pairwise_disjoint: stretched t=%zu i=%zu", t, i);
        part[t][2 * i + 1] = saved;
      }
    }
  }
}

// ================================ Normalize =================================

// The canonical set holding the codepoints whose bits are set in mask.
static size_t mask_to_set(unsigned mask, uint32_t* out) {
  size_t n = 0;
  for (uint32_t v = 0; v < 32; v++) {
    if (!((mask >> v) & 1))
      continue;
    if (n > 0 && out[2 * n - 1] == v) {
      out[2 * n - 1] = v + 1;
    } else {
      out[2 * n] = v;
      out[2 * n + 1] = v + 1;
      n++;
    }
  }
  return n;
}

static void test_normalize(void) {
  enum { V = 7, K = 3 };

  // every list of up to 3 arbitrary intervals over [0, 6], including empty,
  // reversed, overlapping and adjacent ones, against a bitmask
  for (int k = 0; k <= K; k++) {
    unsigned combos = 1;
    for (int t = 0; t < 2 * k; t++)
      combos *= V;
    for (unsigned c = 0; c < combos; c++) {
      uint32_t x[2 * K];
      unsigned code = c, mask = 0;
      for (int t = 0; t < 2 * k; t++) {
        x[t] = code % V;
        code /= V;
      }
      for (int t = 0; t < k; t++)
        for (uint32_t v = x[2 * t]; v < x[2 * t + 1]; v++)
          mask |= 1u << v;
      uint32_t want[2 * V];
      size_t wn = mask_to_set(mask, want);
      size_t gn = ucpset_normalize(x, (size_t)k);
      ASSERT(intervals_equal(x, gn, want, wn), "normalize k=%d c=%u", k, c);
      validate_intervals(x, gn, "normalize");
    }
  }

  // hi is clamped to the Unicode bound; what lies above it is dropped
  {
    uint32_t y[6] = {0x10FFF0, 0xFFFFFFFF, 5, 5, UCPSET_BOUND_MAX, UCPSET_BOUND_MAX + 10};
    uint32_t want[] = {0x10FFF0, UCPSET_BOUND_MAX};
    size_t n = ucpset_normalize(y, 3);
    expect_eq("normalize: clamp", y, n, want, 1);
    ASSERT(ucpset_normalize(NULL, 0) == 0, "normalize: empty");
  }

  // already canonical input comes back unchanged
  {
    uint32_t y[] = {10, 20, 30, 40, 50, 60};
    uint32_t want[] = {10, 20, 30, 40, 50, 60};
    size_t n = ucpset_normalize(y, 3);
    expect_eq("normalize: canonical input", y, n, want, 3);
  }

  // random lists against ucpset_insert_interval, in buffers of exactly n
  // intervals, and the sort on its own at depth limits that force the
  // heapsort fallback
  {
    uint64_t r = 12345;
    for (int t = 0; t < 20000; t++) {
      size_t n = (size_t)(r % 300);
      uint32_t mod = t % 3 == 0 ? 4 : t % 3 == 1 ? 100 : 0x110000;
      uint32_t* g = (uint32_t*)malloc((n ? n : 1) * 2 * sizeof(uint32_t));
      uint32_t* w = (uint32_t*)malloc((n ? n : 1) * 2 * sizeof(uint32_t));
      ASSERT(g && w, "OOM");
      size_t wn = 0;
      for (size_t k = 0; k < n; k++) {
        r ^= r << 13; r ^= r >> 7; r ^= r << 17;
        uint32_t lo = (uint32_t)(r >> 16) % mod;
        uint32_t hi = t % 5 == 0 ? lo : lo + (uint32_t)(r >> 40) % 50;
        g[2 * k] = t % 7 == 0 ? hi : lo;
        g[2 * k + 1] = t % 7 == 0 ? lo : hi;
        wn = ucpset_insert_interval(w, wn, g[2 * k], g[2 * k + 1]);
      }
      size_t gn = ucpset_normalize(g, n);
      ASSERT(intervals_equal(g, gn, w, wn), "normalize random t=%d", t);
      validate_intervals(g, gn, "normalize random");

      for (size_t k = 0; k < n; k++)
        g[2 * k] = (uint32_t)((k * 2654435761u) % 97);
      ucpset_sort_(g, n, (unsigned)(t % 3));
      for (size_t k = 1; k < n; k++)
        ASSERT(g[2 * k - 2] <= g[2 * k], "sort depth=%d t=%d", t % 3, t);

      free(g);
      free(w);
      r ^= r << 13; r ^= r >> 7; r ^= r << 17;
    }
  }

  // codepoints: every list of up to 4 values in [0, 6], in a buffer of
  // exactly 2n words
  for (int k = 0; k <= 4; k++) {
    unsigned combos = 1;
    for (int t = 0; t < k; t++)
      combos *= V;
    for (unsigned c = 0; c < combos; c++) {
      uint32_t* g = (uint32_t*)malloc((k ? 2 * k : 1) * sizeof(uint32_t));
      ASSERT(g, "OOM");
      unsigned code = c, mask = 0;
      for (int t = 0; t < k; t++) {
        g[t] = code % V;
        mask |= 1u << g[t];
        code /= V;
      }
      uint32_t want[2 * V];
      size_t wn = mask_to_set(mask, want);
      size_t gn = ucpset_normalize_codepoints(g, (size_t)k);
      ASSERT(intervals_equal(g, gn, want, wn), "normalize_codepoints k=%d c=%u", k, c);
      free(g);
    }
  }

  // codepoints above U+10FFFF are dropped
  {
    uint32_t z[8] = {UCPSET_CODEPOINT_MAX, 0x110000, 0xFFFFFFFF, UCPSET_CODEPOINT_MAX - 1};
    uint32_t want[] = {UCPSET_CODEPOINT_MAX - 1, UCPSET_BOUND_MAX};
    size_t n = ucpset_normalize_codepoints(z, 4);
    expect_eq("normalize_codepoints: bounds", z, n, want, 1);
    ASSERT(ucpset_normalize_codepoints(NULL, 0) == 0, "normalize_codepoints: empty");
  }
}

// ============================ Galloping (lopsided) ==========================

// Check every sweep-based operation on (x, y) against the bitvec oracle.
static void gallop_check_pair(const uint32_t* x, size_t xcount,
                              const uint32_t* y, size_t ycount,
                              bitvec_t* bx, bitvec_t* by, bitvec_t* bt,
                              uint32_t* got, uint32_t* exp, int iter) {
  bv_from_intervals(bx, x, xcount);
  bv_from_intervals(by, y, ycount);

  for (int op = 0; op < 4; op++) {
    static const char* const names[] = {"union", "intersection", "difference", "xor"};
    size_t got_count;

    if (op == 0) {
      bv_union(bt, bx, by);
      got_count = ucpset_union(got, x, xcount, y, ycount);
    } else if (op == 1) {
      bv_intersection(bt, bx, by);
      got_count = ucpset_intersection(got, x, xcount, y, ycount);
    } else if (op == 2) {
      bv_difference(bt, bx, by);
      got_count = ucpset_difference(got, x, xcount, y, ycount);
    } else {
      bv_xor(bt, bx, by);
      got_count = ucpset_xor(got, x, xcount, y, ycount);
    }

    size_t exp_count = bv_to_intervals(bt, exp);
    ASSERT(intervals_equal(got, got_count, exp, exp_count),
           "gallop %s iter=%d", names[op], iter);
    validate_intervals(got, got_count, "gallop");

    // the buffer-free count of shared codepoints
    if (op == 1)
      ASSERT(ucpset_cardinality_intersection(x, xcount, y, ycount) == bv_cardinality(bt),
             "gallop cardinality_intersection iter=%d", iter);
  }

  // in-place forms, in a buffer of exactly the required capacity
  {
    static const char* const names[] = {"union", "intersection", "difference", "xor"};
    size_t cap = 2 * (xcount + ycount);
    uint32_t* buf = (uint32_t*)malloc((cap ? cap : 1) * sizeof(uint32_t));
    ASSERT(buf, "OOM");

    for (int op = 0; op < 4; op++) {
      size_t got_count;
      if (xcount)
        memcpy(buf, x, xcount * 2 * sizeof(uint32_t));

      if (op == 0) {
        bv_union(bt, bx, by);
        got_count = ucpset_inplace_union(buf, xcount, y, ycount);
      } else if (op == 1) {
        bv_intersection(bt, bx, by);
        got_count = ucpset_inplace_intersection(buf, xcount, y, ycount);
      } else if (op == 2) {
        bv_difference(bt, bx, by);
        got_count = ucpset_inplace_difference(buf, xcount, y, ycount);
      } else {
        bv_xor(bt, bx, by);
        got_count = ucpset_inplace_xor(buf, xcount, y, ycount);
      }

      size_t exp_count = bv_to_intervals(bt, exp);
      ASSERT(intervals_equal(buf, got_count, exp, exp_count),
             "gallop inplace %s iter=%d", names[op], iter);
    }
    free(buf);
  }

  bool exp_subset = true, exp_disjoint = true;
  for (size_t p = 0; p < bx->N; p++) {
    if (bx->bits[p] && !by->bits[p]) exp_subset = false;
    if (bx->bits[p] && by->bits[p]) exp_disjoint = false;
  }
  ASSERT(ucpset_subset(x, xcount, y, ycount) == exp_subset, "gallop subset iter=%d", iter);
  ASSERT(ucpset_superset(y, ycount, x, xcount) == exp_subset, "gallop superset iter=%d", iter);
  ASSERT(ucpset_disjoint(x, xcount, y, ycount) == exp_disjoint, "gallop disjoint iter=%d", iter);
}

/*
 * One dense set against a sparse one, in both orders, so that runs on one
 * side exceed UCPSET_GALLOP_MIN and the gallop paths are taken at the
 * default threshold (the exhaustive tests' sets are too small for that).
 */
static void test_gallop(void) {
  enum { GN = 1536, GITERS = 600 };
  static uint32_t dense[GN], sparse[GN], got[2 * GN], exp[2 * GN];

  bitvec_t gx = bv_make(GN), gy = bv_make(GN), gt = bv_make(GN);

  // ucpset_gallop_upper_bound_hi agrees with ucpset_upper_bound_hi for every key
  // and every starting offset
  {
    uint32_t x[40];
    for (uint32_t t = 0; t < 20; t++) {
      x[2 * t] = 3 * t + 1;
      x[2 * t + 1] = 3 * t + 3;
    }
    for (size_t n = 0; n <= 20; n++)
      for (size_t off = 0; off <= n; off++)
        for (uint32_t key = 0; key < 64; key++)
          ASSERT(ucpset_gallop_upper_bound_hi(x + 2 * off, n - off, key) ==
                 ucpset_upper_bound_hi(x + 2 * off, n - off, key),
                 "gallop_upper_bound_hi n=%zu off=%zu key=%u", n, off, key);
  }

  for (int iter = 0; iter < GITERS; iter++) {
    // dense: short intervals separated by short gaps
    size_t dcount = 0;
    for (uint32_t p = rng_bounded(4); p + 4 < GN;) {
      uint32_t len = 1 + rng_bounded(3);
      dense[2 * dcount] = p;
      dense[2 * dcount + 1] = p + len;
      dcount++;
      p += len + 1 + rng_bounded(3);
    }

    // sparse: a few intervals, chosen one of five ways
    size_t scount = 0;
    uint32_t kind = rng_bounded(5);
    size_t want = rng_bounded(6);
    for (size_t t = 0; t < want; t++) {
      uint32_t lo, hi;
      if (kind == 0) {
        // anywhere, any length
        lo = rng_bounded(GN - 1);
        hi = lo + 1 + rng_bounded(GN / 4);
      } else if (kind == 1) {
        // endpoints on the dense set's own boundaries (touching, not overlapping)
        lo = dense[rng_bounded((uint32_t)(2 * dcount))];
        hi = dense[rng_bounded((uint32_t)(2 * dcount))];
      } else if (kind == 2) {
        // exact copies of dense intervals: sparse is a subset of dense
        size_t d = rng_bounded((uint32_t)dcount);
        lo = dense[2 * d];
        hi = dense[2 * d + 1];
      } else if (kind == 4) {
        // exactly the gap after a dense interval: disjoint from dense
        size_t d = rng_bounded((uint32_t)(dcount - 1));
        lo = dense[2 * d + 1];
        hi = dense[2 * d + 2];
      } else {
        // spans covering whole stretches of the dense set
        size_t a = rng_bounded((uint32_t)dcount);
        size_t b = a + rng_bounded((uint32_t)(dcount - a));
        lo = dense[2 * a];
        hi = dense[2 * b + 1];
      }
      if (hi > GN) hi = GN;
      scount = ucpset_insert_interval(sparse, scount, lo, hi);
    }
    if (kind == 3 && rng_bounded(2)) {
      // one span over everything: dense is a subset of sparse
      scount = ucpset_insert_interval(sparse, scount, dense[0], dense[2 * dcount - 1]);
    }
    validate_intervals(sparse, scount, "gallop sparse input");

    gallop_check_pair(dense, dcount, sparse, scount, &gx, &gy, &gt, got, exp, iter);
    gallop_check_pair(sparse, scount, dense, dcount, &gx, &gy, &gt, got, exp, iter);
  }

  // A run of exactly UCPSET_GALLOP_MIN intervals that is also the end of
  // its set, in a gap of the other: the run is long enough to gallop, but
  // nothing is left to gallop over.
  {
    enum { RUN = UCPSET_GALLOP_MIN };
    uint32_t wide[] = {0, 10, 1400, 1410};
    uint32_t run[2 * (RUN + 1)];
    run[0] = 5;
    run[1] = 12;
    for (uint32_t t = 0; t < RUN; t++) {
      run[2 * t + 2] = 20 + 4 * t;
      run[2 * t + 3] = 22 + 4 * t;
    }
    ASSERT(run[2 * RUN + 1] < 1400, "gallop run test needs a smaller UCPSET_GALLOP_MIN");
    gallop_check_pair(run, RUN + 1, wide, 2, &gx, &gy, &gt, got, exp, -1);
    gallop_check_pair(wide, 2, run, RUN + 1, &gx, &gy, &gt, got, exp, -2);
  }

  // The same for symmetric difference, which only copies runs when one set
  // is much larger: 4 * UCPSET_GALLOP_MIN intervals against two, the last
  // UCPSET_GALLOP_MIN of them between the two and ending their set.
  {
    enum { RUN = UCPSET_GALLOP_MIN };
    uint32_t two[] = {1000, 1001, 1400, 1410};
    uint32_t many[2 * 4 * RUN];
    for (uint32_t t = 0; t < 3 * RUN; t++) {
      many[2 * t] = 10 + 4 * t;
      many[2 * t + 1] = 12 + 4 * t;
    }
    for (uint32_t t = 0; t < RUN; t++) {
      many[2 * (3 * RUN + t)] = 1100 + 4 * t;
      many[2 * (3 * RUN + t) + 1] = 1102 + 4 * t;
    }
    ASSERT(many[2 * 3 * RUN - 1] < 1000 && many[2 * 4 * RUN - 1] < 1400,
           "gallop xor run test needs a smaller UCPSET_GALLOP_MIN");
    gallop_check_pair(many, 4 * RUN, two, 2, &gx, &gy, &gt, got, exp, -3);
    gallop_check_pair(two, 2, many, 4 * RUN, &gx, &gy, &gt, got, exp, -4);
  }

  bv_free(&gx);
  bv_free(&gy);
  bv_free(&gt);
}

// ================================== Main ===================================

int main(void) {
  printf("Running canonicality tests...\n");
  test_canonicality();
  printf(" Canonicality OK\n");

  printf("Running union tests...\n");
  test_union_manual();
  printf(" Union OK\n");

  printf("Running intersection tests...\n");
  test_intersection_manual();
  printf(" Intersection OK\n");

  printf("Running difference tests...\n");
  test_difference_manual();
  printf(" Difference OK\n");

  printf("Running xor tests...\n");
  test_xor_manual();
  printf(" Xor OK\n");

  printf("Running complement tests...\n");
  test_complement();
  printf(" Complement OK\n");

  printf("Running predicate tests...\n");
  test_predicates();
  printf(" Predicates OK\n");

  printf("Running membership tests...\n");
  test_membership();
  printf(" Membership OK\n");

  printf("Running insert tests...\n");
  test_insert();
  printf(" Insert OK\n");

  printf("Running insert interval tests...\n");
  test_insert_interval();
  printf(" Insert interval OK\n");

  printf("Running remove tests...\n");
  test_remove();
  printf(" Remove OK\n");

  printf("Running remove interval tests...\n");
  test_remove_interval();
  printf(" Remove interval OK\n");

  printf("Running cardinality tests...\n");
  test_cardinality();
  printf(" Cardinality OK\n");

  printf("Running boolean identity tests...\n");
  test_boolean_identities();
  printf(" Boolean identities OK\n");

  printf("Running search tests...\n");
  test_search();
  printf(" Search OK\n");

  printf("Running inplace fast-path tests...\n");
  test_inplace_fastpaths();
  printf(" Inplace fast-paths OK\n");

  printf("Running inplace cardinality fast-path tests...\n");
  test_inplace_fastpaths_cardinality();
  printf(" Inplace cardinality fast-paths OK\n");

  printf("Running merge cardinality fast-path tests...\n");
  test_merge_fastpaths_cardinality();
  printf(" Merge cardinality fast-paths OK\n");

  printf("Running membership edge case tests...\n");
  test_membership_edge();
  printf(" Membership edge cases OK\n");

  printf("Running insert interval edge case tests...\n");
  test_insert_interval_edge();
  printf(" Insert interval edge cases OK\n");

  printf("Running remove interval edge case tests...\n");
  test_remove_interval_edge();
  printf(" Remove interval edge cases OK\n");

  printf("Running cardinality edge case tests...\n");
  test_cardinality_edge();
  printf(" Cardinality edge cases OK\n");

  printf("Running predicate edge case tests...\n");
  test_predicate_edge();
  printf(" Predicate edge cases OK\n");

  printf("Running strict canonical output tests...\n");
  test_strict_canonical_output();
  printf(" Strict canonical OK\n");

  printf("Running exhaustive tests (N=8)...\n");
  test_exhaustive_small();
  printf(" Exhaustive OK\n");

  printf("Running fuzz tests (N=256, 1000 iterations)...\n");
  test_fuzz();
  printf(" Fuzz OK\n");

  printf("Running single-interval operation tests...\n");
  test_interval_ops();
  printf(" Single-interval operations OK\n");

  printf("Running editing tests (move, find, slice, splice)...\n");
  test_editing();
  printf(" Editing OK\n");

  printf("Running contains-with-hint tests...\n");
  test_contains_hint();
  printf(" Contains with hint OK\n");

  printf("Running pairwise disjoint tests...\n");
  test_pairwise_disjoint();
  printf(" Pairwise disjoint OK\n");

  printf("Running normalize tests...\n");
  test_normalize();
  printf(" Normalize OK\n");

  printf("Running gallop tests (dense vs sparse, GALLOP_MIN=%d)...\n", UCPSET_GALLOP_MIN);
  test_gallop();
  printf(" Gallop OK\n");

  printf("\n All tests passed!\n");
  return 0;
}
