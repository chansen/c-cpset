#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "ucpset.h"

// =========================== Test infrastructure ===========================

#define DIE(...) do { \
    fprintf(stderr, "TEST FAILURE: " __VA_ARGS__); \
    fprintf(stderr, " @ %s:%d\n", __FILE__, __LINE__); \
    exit(1); \
} while (0)

#define ASSERT(cond, ...) do { if (!(cond)) { DIE(__VA_ARGS__); } } while (0)

// ========================== ucprange_t basics ==============================

static void test_ucprange_basics(void) {
  // construction
  {
    ucprange_t r = ucprange(10, 20);
    ASSERT(r.lo == 10 && r.hi == 20, "ucprange: construction");
  }

  // count
  {
    ASSERT(ucprange_cardinality(ucprange(0, 100)) == 100, "cardinality: 100");
    ASSERT(ucprange_cardinality(ucprange(5, 5)) == 0, "cardinality: empty");
    ASSERT(ucprange_cardinality(ucprange(10, 5)) == 0, "cardinality: reversed");
  }

  // is_empty
  {
    ASSERT(!ucprange_is_empty(ucprange(0, 1)), "is_empty: [0,1) not empty");
    ASSERT(ucprange_is_empty(ucprange(5, 5)), "is_empty: [5,5) empty");
    ASSERT(ucprange_is_empty(ucprange(10, 3)), "is_empty: reversed empty");
  }

  // is_singleton
  {
    ASSERT(ucprange_is_singleton(ucprange(0, 1)), "is_singleton: [0,1)");
    ASSERT(ucprange_is_singleton(ucprange(99, 100)), "is_singleton: [99,100)");
    ASSERT(!ucprange_is_singleton(ucprange(0, 2)), "is_singleton: [0,2) not");
    ASSERT(!ucprange_is_singleton(ucprange(5, 5)), "is_singleton: empty not");
    ASSERT(!ucprange_is_singleton(ucprange(10, 5)), "is_singleton: reversed not");
    ASSERT(ucprange_is_singleton(ucprange(0, 1)) == (ucprange_cardinality(ucprange(0, 1)) == 1),
           "is_singleton: matches cardinality==1");
    ASSERT(ucprange_is_singleton(ucprange(0, 3)) == (ucprange_cardinality(ucprange(0, 3)) == 1),
           "is_singleton: matches cardinality!=1");
  }

  // is_valid
  {
    ASSERT(ucprange_is_valid(ucprange(0, 1)), "is_valid: single codepoint");
    ASSERT(ucprange_is_valid(ucprange(0, 0x110000)), "is_valid: full Unicode");
    ASSERT(ucprange_is_valid(ucprange(0x10FFFF, 0x110000)), "is_valid: last codepoint");
    ASSERT(ucprange_is_valid(ucprange(0x41, 0x5B)), "is_valid: A-Z");
    ASSERT(!ucprange_is_valid(ucprange(5, 5)), "is_valid: empty");
    ASSERT(!ucprange_is_valid(ucprange(10, 5)), "is_valid: reversed");
    ASSERT(!ucprange_is_valid(ucprange(0, 0x110001)), "is_valid: hi exceeds bound");
    ASSERT(!ucprange_is_valid(ucprange(0x110000, 0x110001)), "is_valid: lo at bound");
    ASSERT(!ucprange_is_valid(ucprange(0, UINT32_MAX)), "is_valid: huge hi");
  }

  // equals
  {
    ASSERT(ucprange_equals(ucprange(1, 5), ucprange(1, 5)), "equals: same");
    ASSERT(!ucprange_equals(ucprange(1, 5), ucprange(1, 6)), "equals: diff end");
    ASSERT(!ucprange_equals(ucprange(1, 5), ucprange(2, 5)), "equals: diff start");
  }

  // contains element
  {
    ucprange_t r = ucprange(10, 20);
    ASSERT(!ucprange_contains(r, 9), "contains: below");
    ASSERT(ucprange_contains(r, 10), "contains: start");
    ASSERT(ucprange_contains(r, 15), "contains: middle");
    ASSERT(ucprange_contains(r, 19), "contains: last");
    ASSERT(!ucprange_contains(r, 20), "contains: end exclusive");
  }

  // disjoint (replaces overlaps — disjoint is the complement)
  {
    ucprange_t a = ucprange(10, 20);
    ASSERT(!ucprange_disjoint(a, ucprange(15, 25)), "disjoint: partial overlap");
    ASSERT(!ucprange_disjoint(a, ucprange(5, 15)), "disjoint: partial left");
    ASSERT(!ucprange_disjoint(a, ucprange(12, 18)), "disjoint: contained");
    ASSERT(!ucprange_disjoint(a, ucprange(5, 25)), "disjoint: containing");
    ASSERT(ucprange_disjoint(a, ucprange(20, 30)), "disjoint: adjacent right");
    ASSERT(ucprange_disjoint(a, ucprange(0, 10)), "disjoint: adjacent left");
    ASSERT(ucprange_disjoint(a, ucprange(25, 30)), "disjoint: far right");
    ASSERT(ucprange_disjoint(a, ucprange(0, 5)), "disjoint: far left");
  }

  // adjacent
  {
    ucprange_t a = ucprange(10, 20);
    ASSERT(ucprange_adjacent(a, ucprange(20, 30)), "adjacent: right");
    ASSERT(ucprange_adjacent(a, ucprange(0, 10)), "adjacent: left");
    ASSERT(!ucprange_adjacent(a, ucprange(21, 30)), "adjacent: gap right");
    ASSERT(!ucprange_adjacent(a, ucprange(0, 9)), "adjacent: gap left");
    ASSERT(!ucprange_adjacent(a, ucprange(15, 25)), "adjacent: overlapping");
    ASSERT(!ucprange_adjacent(a, ucprange(10, 20)), "adjacent: equal");
    ASSERT(!ucprange_adjacent(a, ucprange(12, 18)), "adjacent: contained");
    // commutative
    ASSERT(ucprange_adjacent(ucprange(20, 30), a), "adjacent: commutative");
    // adjacent implies disjoint and coalesce
    ASSERT(ucprange_disjoint(a, ucprange(20, 30)), "adjacent: is disjoint");
    ASSERT(ucprange_coalesce(a, ucprange(20, 30)), "adjacent: coalesces");
  }

  // coalesce
  {
    ucprange_t a = ucprange(10, 20);
    // overlapping
    ASSERT(ucprange_coalesce(a, ucprange(15, 25)), "coalesces: overlap right");
    ASSERT(ucprange_coalesce(a, ucprange(5, 15)), "coalesces: overlap left");
    ASSERT(ucprange_coalesce(a, ucprange(12, 18)), "coalesces: contained");
    ASSERT(ucprange_coalesce(a, ucprange(5, 25)), "coalesces: containing");
    ASSERT(ucprange_coalesce(a, ucprange(10, 20)), "coalesces: equal");
    // adjacent (would merge)
    ASSERT(ucprange_coalesce(a, ucprange(20, 30)), "coalesces: adjacent right");
    ASSERT(ucprange_coalesce(a, ucprange(0, 10)), "coalesces: adjacent left");
    // disjoint (gap)
    ASSERT(!ucprange_coalesce(a, ucprange(21, 30)), "coalesces: gap right");
    ASSERT(!ucprange_coalesce(a, ucprange(0, 9)), "coalesces: gap left");
    // commutative
    ASSERT(ucprange_coalesce(ucprange(0, 9), a) == ucprange_coalesce(a, ucprange(0, 9)),
           "coalesces: commutative false");
    ASSERT(ucprange_coalesce(ucprange(20, 30), a) == ucprange_coalesce(a, ucprange(20, 30)),
           "coalesces: commutative true");
    // coalesces == !disjoint for overlapping, but also true for adjacent
    ASSERT(!ucprange_disjoint(a, ucprange(15, 25)) && ucprange_coalesce(a, ucprange(15, 25)),
           "coalesces: overlapping implies both");
    ASSERT(ucprange_disjoint(a, ucprange(20, 30)) && ucprange_coalesce(a, ucprange(20, 30)),
           "coalesces: adjacent is disjoint but coalesces");
    ASSERT(ucprange_disjoint(a, ucprange(21, 30)) && !ucprange_coalesce(a, ucprange(21, 30)),
           "coalesces: gap is disjoint and not coalescing");
  }

  // subset
  {
    ucprange_t a = ucprange(10, 20);
    ASSERT(ucprange_subset(a, ucprange(10, 20)), "subset: equal");
    ASSERT(ucprange_subset(a, ucprange(5, 25)), "subset: within larger");
    ASSERT(ucprange_subset(a, ucprange(10, 25)), "subset: same lo");
    ASSERT(ucprange_subset(a, ucprange(5, 20)), "subset: same hi");
    ASSERT(!ucprange_subset(a, ucprange(12, 18)), "subset: inner smaller");
    ASSERT(!ucprange_subset(a, ucprange(15, 25)), "subset: partial right");
    ASSERT(!ucprange_subset(a, ucprange(5, 15)), "subset: partial left");
    // empty range is subset of anything
    ASSERT(ucprange_subset(ucprange(5, 5), ucprange(0, 10)), "subset: empty in range");
    ASSERT(ucprange_subset(ucprange(5, 5), ucprange(5, 5)), "subset: both empty");
  }

  // superset
  {
    ucprange_t a = ucprange(10, 20);
    ASSERT(ucprange_superset(a, ucprange(10, 20)), "superset: equal");
    ASSERT(ucprange_superset(a, ucprange(12, 18)), "superset: inner");
    ASSERT(ucprange_superset(a, ucprange(10, 15)), "superset: same lo");
    ASSERT(ucprange_superset(a, ucprange(15, 20)), "superset: same hi");
    ASSERT(!ucprange_superset(a, ucprange(5, 25)), "superset: larger");
    ASSERT(!ucprange_superset(a, ucprange(5, 15)), "superset: extends left");
    // superset is inverse of subset
    ASSERT(ucprange_superset(ucprange(5, 25), ucprange(10, 20)),
           "superset: inverse of subset");
    ASSERT(!ucprange_subset(ucprange(5, 25), ucprange(10, 20)),
           "superset: not subset");
  }

  // disjoint
  {
    ucprange_t a = ucprange(10, 20);
    ASSERT(ucprange_disjoint(a, ucprange(20, 30)), "disjoint: adjacent right");
    ASSERT(ucprange_disjoint(a, ucprange(0, 10)), "disjoint: adjacent left");
    ASSERT(ucprange_disjoint(a, ucprange(25, 30)), "disjoint: far right");
    ASSERT(ucprange_disjoint(a, ucprange(0, 5)), "disjoint: far left");
    ASSERT(!ucprange_disjoint(a, ucprange(15, 25)), "disjoint: overlapping");
    ASSERT(!ucprange_disjoint(a, ucprange(10, 20)), "disjoint: equal");
    ASSERT(!ucprange_disjoint(a, ucprange(12, 18)), "disjoint: contained");
  }

  // precedes
  {
    ucprange_t a = ucprange(10, 20);
    ASSERT(ucprange_precedes(a, ucprange(20, 30)), "precedes: adjacent");
    ASSERT(ucprange_precedes(a, ucprange(25, 30)), "precedes: gap");
    ASSERT(!ucprange_precedes(a, ucprange(15, 25)), "precedes: overlapping");
    ASSERT(!ucprange_precedes(a, ucprange(0, 5)), "precedes: before");
    ASSERT(!ucprange_precedes(a, ucprange(10, 20)), "precedes: equal");
    // empty ranges
    ASSERT(ucprange_precedes(ucprange(5, 5), ucprange(5, 10)), "precedes: empty at boundary");
  }

  // succeeds
  {
    ucprange_t a = ucprange(10, 20);
    ASSERT(ucprange_succeeds(a, ucprange(0, 10)), "succeeds: adjacent");
    ASSERT(ucprange_succeeds(a, ucprange(0, 5)), "succeeds: gap");
    ASSERT(!ucprange_succeeds(a, ucprange(5, 15)), "succeeds: overlapping");
    ASSERT(!ucprange_succeeds(a, ucprange(25, 30)), "succeeds: after");
    ASSERT(!ucprange_succeeds(a, ucprange(10, 20)), "succeeds: equal");
    // succeeds is inverse of precedes
    ASSERT(ucprange_succeeds(ucprange(20, 30), ucprange(10, 20)),
           "succeeds: inverse of precedes");
    ASSERT(ucprange_precedes(ucprange(10, 20), ucprange(20, 30)),
           "succeeds: precedes inverse check");
  }

  // prefix
  {
    // overlapping: x=[5,20), y=[10,25) → prefix=[5,10)
    ucprange_t p1 = ucprange_prefix(ucprange(5, 20), ucprange(10, 25));
    ASSERT(p1.lo == 5 && p1.hi == 10, "prefix: left overlap");
    // x entirely before y: x=[0,5), y=[10,20) → prefix=[0,5) (all of x)
    ucprange_t p2 = ucprange_prefix(ucprange(0, 5), ucprange(10, 20));
    ASSERT(p2.lo == 0 && p2.hi == 5, "prefix: disjoint before");
    // x entirely after y: x=[15,25), y=[5,10) → prefix=[15,10) empty
    ucprange_t p3 = ucprange_prefix(ucprange(15, 25), ucprange(5, 10));
    ASSERT(ucprange_is_empty(p3), "prefix: disjoint after empty");
    // x subset of y: x=[12,18), y=[10,20) → prefix=[12,10) empty
    ucprange_t p4 = ucprange_prefix(ucprange(12, 18), ucprange(10, 20));
    ASSERT(ucprange_is_empty(p4), "prefix: subset empty");
    // x superset of y: x=[5,25), y=[10,20) → prefix=[5,10)
    ucprange_t p5 = ucprange_prefix(ucprange(5, 25), ucprange(10, 20));
    ASSERT(p5.lo == 5 && p5.hi == 10, "prefix: superset");
    // equal: empty prefix
    ucprange_t p6 = ucprange_prefix(ucprange(10, 20), ucprange(10, 20));
    ASSERT(ucprange_is_empty(p6), "prefix: equal empty");
    // adjacent: x=[0,10), y=[10,20) → prefix=[0,10) (all of x)
    ucprange_t p7 = ucprange_prefix(ucprange(0, 10), ucprange(10, 20));
    ASSERT(p7.lo == 0 && p7.hi == 10, "prefix: adjacent");
  }

  // suffix
  {
    // overlapping: x=[5,20), y=[0,15) → suffix=[15,20)
    ucprange_t s1 = ucprange_suffix(ucprange(5, 20), ucprange(0, 15));
    ASSERT(s1.lo == 15 && s1.hi == 20, "suffix: right overlap");
    // x entirely after y: x=[15,25), y=[5,10) → suffix=[15,25) (all of x)
    ucprange_t s2 = ucprange_suffix(ucprange(15, 25), ucprange(5, 10));
    ASSERT(s2.lo == 15 && s2.hi == 25, "suffix: disjoint after");
    // x entirely before y: x=[0,5), y=[10,20) → suffix=[10,5) empty
    ucprange_t s3 = ucprange_suffix(ucprange(0, 5), ucprange(10, 20));
    ASSERT(ucprange_is_empty(s3), "suffix: disjoint before empty");
    // x subset of y: x=[12,18), y=[10,20) → suffix=[20,18) empty
    ucprange_t s4 = ucprange_suffix(ucprange(12, 18), ucprange(10, 20));
    ASSERT(ucprange_is_empty(s4), "suffix: subset empty");
    // x superset of y: x=[5,25), y=[10,20) → suffix=[20,25)
    ucprange_t s5 = ucprange_suffix(ucprange(5, 25), ucprange(10, 20));
    ASSERT(s5.lo == 20 && s5.hi == 25, "suffix: superset");
    // equal: empty suffix
    ucprange_t s6 = ucprange_suffix(ucprange(10, 20), ucprange(10, 20));
    ASSERT(ucprange_is_empty(s6), "suffix: equal empty");
    // adjacent: x=[10,20), y=[0,10) → suffix=[10,20) (all of x)
    ucprange_t s7 = ucprange_suffix(ucprange(10, 20), ucprange(0, 10));
    ASSERT(s7.lo == 10 && s7.hi == 20, "suffix: adjacent");
  }

  // intersect
  {
    // overlapping
    ucprange_t i1 = ucprange_intersection(ucprange(5, 15), ucprange(10, 20));
    ASSERT(i1.lo == 10 && i1.hi == 15, "intersect: overlapping");
    // contained
    ucprange_t i2 = ucprange_intersection(ucprange(5, 25), ucprange(10, 20));
    ASSERT(i2.lo == 10 && i2.hi == 20, "intersect: a contains b");
    ucprange_t i3 = ucprange_intersection(ucprange(12, 18), ucprange(10, 20));
    ASSERT(i3.lo == 12 && i3.hi == 18, "intersect: b contains a");
    // equal
    ucprange_t i4 = ucprange_intersection(ucprange(10, 20), ucprange(10, 20));
    ASSERT(i4.lo == 10 && i4.hi == 20, "intersect: equal");
    // disjoint — result is empty (lo >= hi)
    ucprange_t i5 = ucprange_intersection(ucprange(0, 5), ucprange(10, 20));
    ASSERT(ucprange_is_empty(i5), "intersect: disjoint empty");
    // adjacent — result is empty
    ucprange_t i6 = ucprange_intersection(ucprange(0, 10), ucprange(10, 20));
    ASSERT(ucprange_is_empty(i6), "intersect: adjacent empty");
    // commutative
    ucprange_t ia = ucprange_intersection(ucprange(5, 15), ucprange(10, 20));
    ucprange_t ib = ucprange_intersection(ucprange(10, 20), ucprange(5, 15));
    ASSERT(ucprange_equals(ia, ib), "intersect: commutative");
  }

  // span
  {
    // disjoint ranges
    ucprange_t s1 = ucprange_span(ucprange(0, 5), ucprange(10, 20));
    ASSERT(s1.lo == 0 && s1.hi == 20, "span: disjoint");
    // overlapping
    ucprange_t s2 = ucprange_span(ucprange(5, 15), ucprange(10, 20));
    ASSERT(s2.lo == 5 && s2.hi == 20, "span: overlapping");
    // contained
    ucprange_t s3 = ucprange_span(ucprange(5, 25), ucprange(10, 20));
    ASSERT(s3.lo == 5 && s3.hi == 25, "span: a contains b");
    ucprange_t s4 = ucprange_span(ucprange(12, 18), ucprange(10, 20));
    ASSERT(s4.lo == 10 && s4.hi == 20, "span: b contains a");
    // equal
    ucprange_t s5 = ucprange_span(ucprange(10, 20), ucprange(10, 20));
    ASSERT(s5.lo == 10 && s5.hi == 20, "span: equal");
    // commutative
    ucprange_t sa = ucprange_span(ucprange(3, 7), ucprange(15, 25));
    ucprange_t sb = ucprange_span(ucprange(15, 25), ucprange(3, 7));
    ASSERT(ucprange_equals(sa, sb), "span: commutative");
    // with empty range
    ucprange_t s6 = ucprange_span(ucprange(10, 20), ucprange(5, 5));
    ASSERT(s6.lo == 5 && s6.hi == 20, "span: with empty");
    // adjacent
    ucprange_t s7 = ucprange_span(ucprange(0, 10), ucprange(10, 20));
    ASSERT(s7.lo == 0 && s7.hi == 20, "span: adjacent");
  }

  // plus / minus (shift)
  {
    ucprange_t r = ucprange(10, 20);
    ucprange_t p = ucprange_plus(r, 5);
    ASSERT(p.lo == 15 && p.hi == 25, "plus: shift right");
    ucprange_t m = ucprange_minus(r, 3);
    ASSERT(m.lo == 7 && m.hi == 17, "minus: shift left");
    // zero shift is identity
    ASSERT(ucprange_equals(ucprange_plus(r, 0), r), "plus: zero");
    ASSERT(ucprange_equals(ucprange_minus(r, 0), r), "minus: zero");
    // plus then minus is identity
    ASSERT(ucprange_equals(ucprange_minus(ucprange_plus(r, 7), 7), r),
           "plus/minus: roundtrip");
    // cardinality preserved
    ASSERT(ucprange_cardinality(ucprange_plus(r, 100)) == ucprange_cardinality(r),
           "plus: preserves cardinality");
    ASSERT(ucprange_cardinality(ucprange_minus(r, 5)) == ucprange_cardinality(r),
           "minus: preserves cardinality");
    // shift from origin
    ucprange_t z = ucprange(0, 10);
    ucprange_t zp = ucprange_plus(z, 0x10000);
    ASSERT(zp.lo == 0x10000 && zp.hi == 0x1000A, "plus: large shift");
  }

  // consistency: disjoint iff precedes or succeeds
  {
    ucprange_t pairs[][2] = {
      {{0, 5}, {10, 20}},   {{10, 20}, {20, 30}},
      {{10, 20}, {15, 25}}, {{10, 20}, {10, 20}},
      {{0, 10}, {5, 15}},   {{5, 5}, {10, 20}},
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
      ucprange_t a = pairs[i][0], b = pairs[i][1];
      bool dis = ucprange_disjoint(a, b);
      bool ps = ucprange_precedes(a, b) || ucprange_succeeds(a, b);
      ASSERT(dis == ps, "consistency: disjoint == precedes|succeeds i=%zu", i);
    }
  }

  // compare
  {
    // less by lo
    ASSERT(ucprange_compare(ucprange(5, 20), ucprange(10, 20)) == -1, "compare: lo <");
    ASSERT(ucprange_compare(ucprange(10, 20), ucprange(5, 20)) == 1, "compare: lo >");
    // equal lo, less by hi
    ASSERT(ucprange_compare(ucprange(10, 15), ucprange(10, 20)) == -1, "compare: hi <");
    ASSERT(ucprange_compare(ucprange(10, 25), ucprange(10, 20)) == 1, "compare: hi >");
    // equal
    ASSERT(ucprange_compare(ucprange(10, 20), ucprange(10, 20)) == 0, "compare: equal");
    ASSERT(ucprange_compare(ucprange(0, 0), ucprange(0, 0)) == 0, "compare: both empty");
    // antisymmetry
    ASSERT(ucprange_compare(ucprange(5, 15), ucprange(10, 20)) ==
           -ucprange_compare(ucprange(10, 20), ucprange(5, 15)), "compare: antisymmetric");
  }

  // relation
  {
    // -4: x precedes y with gap
    ASSERT(ucprange_relation(ucprange(0, 5), ucprange(10, 20)) == -4,
           "relation: -4 gap");
    ASSERT(ucprange_relation(ucprange(0, 9), ucprange(10, 20)) == -4,
           "relation: -4 gap by one");

    // -3: x adjacent to y from left (coalesces)
    ASSERT(ucprange_relation(ucprange(0, 10), ucprange(10, 20)) == -3,
           "relation: -3 adjacent left");
    ASSERT(ucprange_relation(ucprange(5, 10), ucprange(10, 20)) == -3,
           "relation: -3 adjacent left short");

    // -2: x overlaps y from left
    ASSERT(ucprange_relation(ucprange(0, 15), ucprange(10, 20)) == -2,
           "relation: -2 left overlap");
    ASSERT(ucprange_relation(ucprange(5, 18), ucprange(10, 20)) == -2,
           "relation: -2 left overlap partial");

    // -1: x is proper superset
    ASSERT(ucprange_relation(ucprange(5, 25), ucprange(10, 20)) == -1,
           "relation: -1 proper superset");
    ASSERT(ucprange_relation(ucprange(5, 20), ucprange(10, 20)) == -1,
           "relation: -1 superset same hi");
    // same lo, x.hi > y.hi
    ASSERT(ucprange_relation(ucprange(10, 25), ucprange(10, 20)) == -1,
           "relation: -1 superset same lo");

    // 0: equal
    ASSERT(ucprange_relation(ucprange(10, 20), ucprange(10, 20)) == 0,
           "relation: 0 equal");
    ASSERT(ucprange_relation(ucprange(0, 0), ucprange(0, 0)) == 0,
           "relation: 0 both empty");

    // 1: x is proper subset
    ASSERT(ucprange_relation(ucprange(12, 18), ucprange(10, 20)) == 1,
           "relation: 1 proper subset");
    ASSERT(ucprange_relation(ucprange(10, 18), ucprange(10, 20)) == 1,
           "relation: 1 subset same lo");
    ASSERT(ucprange_relation(ucprange(12, 20), ucprange(10, 20)) == 1,
           "relation: 1 subset same hi, x.lo > y.lo");

    // 2: x overlaps y from right
    ASSERT(ucprange_relation(ucprange(15, 25), ucprange(10, 20)) == 2,
           "relation: 2 right overlap");

    // 3: x adjacent to y from right (coalesces)
    ASSERT(ucprange_relation(ucprange(20, 30), ucprange(10, 20)) == 3,
           "relation: 3 adjacent right");
    ASSERT(ucprange_relation(ucprange(20, 25), ucprange(10, 20)) == 3,
           "relation: 3 adjacent right short");

    // 4: x succeeds y with gap
    ASSERT(ucprange_relation(ucprange(25, 30), ucprange(10, 20)) == 4,
           "relation: 4 gap");
    ASSERT(ucprange_relation(ucprange(21, 30), ucprange(10, 20)) == 4,
           "relation: 4 gap by one");
  }

  // relation consistency with other predicates
  {
    ucprange_t cases[][2] = {
      {{0, 5}, {10, 20}},   {{0, 15}, {10, 20}},  {{5, 25}, {10, 20}},
      {{10, 20}, {10, 20}}, {{12, 18}, {10, 20}},  {{15, 25}, {10, 20}},
      {{25, 30}, {10, 20}}, {{10, 25}, {10, 20}},  {{10, 18}, {10, 20}},
      {{20, 30}, {10, 20}}, {{5, 20}, {10, 20}},   {{12, 20}, {10, 20}},
      {{0, 9}, {10, 20}},   {{0, 10}, {10, 20}},   {{21, 30}, {10, 20}},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      ucprange_t a = cases[i][0], b = cases[i][1];
      int rel = ucprange_relation(a, b);
      // disjoint iff |rel| >= 3 (adjacent or gap)
      ASSERT(ucprange_disjoint(a, b) == (rel <= -3 || rel >= 3),
             "relation consistency: disjoint i=%zu", i);
      // adjacent iff |rel| == 3
      ASSERT(ucprange_adjacent(a, b) == (rel == -3 || rel == 3),
             "relation consistency: adjacent i=%zu", i);
      // coalesce iff |rel| <= 3 (overlap, containment, equal, or adjacent)
      ASSERT(ucprange_coalesce(a, b) == (rel >= -3 && rel <= 3),
             "relation consistency: coalesce i=%zu", i);
      // precedes iff rel <= -3 (adjacent or gap before)
      ASSERT(ucprange_precedes(a, b) == (rel <= -3),
             "relation consistency: precedes i=%zu", i);
      // succeeds iff rel >= 3 (adjacent or gap after)
      ASSERT(ucprange_succeeds(a, b) == (rel >= 3),
             "relation consistency: succeeds i=%zu", i);
      // equals iff rel == 0
      ASSERT(ucprange_equals(a, b) == (rel == 0),
             "relation consistency: equals i=%zu", i);
      // antisymmetry
      ASSERT(ucprange_relation(b, a) == -rel,
             "relation consistency: antisymmetry i=%zu", i);
    }
  }
}

// ========================= ucpset_*_range wrappers =========================

static void test_insert_range(void) {
  // basic insert
  {
    uint32_t x[10] = {0};
    size_t n = 0;
    n = ucpset_insert_range(x, n, ucprange(10, 20));
    ASSERT(n == 1, "insert_range: one interval");
    ASSERT(x[0] == 10 && x[1] == 20, "insert_range: values");
  }

  // merge adjacent
  {
    uint32_t x[10] = {10, 20};
    size_t n = 1;
    n = ucpset_insert_range(x, n, ucprange(20, 30));
    ASSERT(n == 1, "insert_range merge: one interval");
    ASSERT(x[0] == 10 && x[1] == 30, "insert_range merge: merged");
  }

  // merge overlapping
  {
    uint32_t x[10] = {10, 20};
    size_t n = 1;
    n = ucpset_insert_range(x, n, ucprange(15, 25));
    ASSERT(n == 1, "insert_range overlap: one interval");
    ASSERT(x[0] == 10 && x[1] == 25, "insert_range overlap: extended");
  }

  // empty range is no-op
  {
    uint32_t x[10] = {10, 20};
    size_t n = 1;
    n = ucpset_insert_range(x, n, ucprange(5, 5));
    ASSERT(n == 1, "insert_range empty: no change");
    ASSERT(x[0] == 10 && x[1] == 20, "insert_range empty: values unchanged");
  }

  // matches insert_interval for all cases
  {
    uint32_t a[20], b[20];
    uint32_t base[] = {5, 10, 20, 30, 40, 50};
    ucprange_t ranges[] = {
      ucprange(0, 3), ucprange(10, 20), ucprange(25, 45),
      ucprange(50, 60), ucprange(0, 100), ucprange(15, 15),
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
      memcpy(a, base, sizeof(base));
      memcpy(b, base, sizeof(base));
      size_t na = ucpset_insert_interval(a, 3, ranges[i].lo, ranges[i].hi);
      size_t nb = ucpset_insert_range(b, 3, ranges[i]);
      ASSERT(na == nb, "insert_range matches interval: count i=%zu", i);
      ASSERT(memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0,
             "insert_range matches interval: data i=%zu", i);
    }
  }
}

static void test_remove_range(void) {
  // basic remove
  {
    uint32_t x[10] = {0, 100};
    size_t n = 1;
    n = ucpset_remove_range(x, n, ucprange(30, 60));
    ASSERT(n == 2, "remove_range: split into two");
    ASSERT(x[0] == 0 && x[1] == 30, "remove_range: left part");
    ASSERT(x[2] == 60 && x[3] == 100, "remove_range: right part");
  }

  // remove from start
  {
    uint32_t x[10] = {10, 50};
    size_t n = 1;
    n = ucpset_remove_range(x, n, ucprange(10, 30));
    ASSERT(n == 1, "remove_range start: one interval");
    ASSERT(x[0] == 30 && x[1] == 50, "remove_range start: trimmed");
  }

  // remove entire
  {
    uint32_t x[10] = {10, 20};
    size_t n = 1;
    n = ucpset_remove_range(x, n, ucprange(0, 100));
    ASSERT(n == 0, "remove_range entire: empty");
  }

  // empty range is no-op
  {
    uint32_t x[10] = {10, 20};
    size_t n = 1;
    n = ucpset_remove_range(x, n, ucprange(15, 15));
    ASSERT(n == 1, "remove_range empty: no change");
  }

  // matches remove_interval for all cases
  {
    uint32_t a[20], b[20];
    uint32_t base[] = {5, 10, 20, 30, 40, 50};
    ucprange_t ranges[] = {
      ucprange(0, 3), ucprange(7, 25), ucprange(5, 50),
      ucprange(0, 100), ucprange(10, 10), ucprange(35, 45),
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
      memcpy(a, base, sizeof(base));
      memcpy(b, base, sizeof(base));
      size_t na = ucpset_remove_interval(a, 3, ranges[i].lo, ranges[i].hi);
      size_t nb = ucpset_remove_range(b, 3, ranges[i]);
      ASSERT(na == nb, "remove_range matches interval: count i=%zu", i);
      ASSERT(memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0,
             "remove_range matches interval: data i=%zu", i);
    }
  }
}

static void test_contains_range(void) {
  uint32_t x[] = {10, 20, 30, 40, 50, 60};
  size_t n = 3;

  // fully contained
  ASSERT(ucpset_contains_range(x, n, ucprange(10, 20)), "contains_range: exact");
  ASSERT(ucpset_contains_range(x, n, ucprange(12, 18)), "contains_range: inside");
  ASSERT(ucpset_contains_range(x, n, ucprange(50, 60)), "contains_range: last exact");

  // partially outside
  ASSERT(!ucpset_contains_range(x, n, ucprange(5, 15)), "contains_range: extends left");
  ASSERT(!ucpset_contains_range(x, n, ucprange(15, 25)), "contains_range: extends right");

  // spans gap
  ASSERT(!ucpset_contains_range(x, n, ucprange(15, 35)), "contains_range: spans gap");
  ASSERT(!ucpset_contains_range(x, n, ucprange(10, 60)), "contains_range: spans all");

  // empty range is vacuously contained, wherever it lies
  ASSERT(ucpset_contains_range(x, n, ucprange(15, 15)), "contains_range: empty");
  ASSERT(ucpset_contains_range(x, n, ucprange(25, 25)), "contains_range: empty in gap");
  ASSERT(ucpset_contains_range(x, n, ucprange(30, 20)), "contains_range: reversed");
  ASSERT(ucpset_contains_range(NULL, 0, ucprange(15, 15)), "contains_range: empty in empty set");

  // matches contains_interval
  {
    ucprange_t ranges[] = {
      ucprange(0, 5), ucprange(10, 20), ucprange(15, 35),
      ucprange(30, 40), ucprange(55, 65), ucprange(0, 100),
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
      bool a = ucpset_contains_interval(x, n, ranges[i].lo, ranges[i].hi);
      bool b = ucpset_contains_range(x, n, ranges[i]);
      ASSERT(a == b, "contains_range matches interval: i=%zu", i);
    }
  }
}

// ========================= exhaustive verification =========================

#define N 8

typedef struct {
  uint8_t bits[N];
} bitvec_t;

static void bv_clear(bitvec_t* bv) { memset(bv, 0, sizeof(*bv)); }

static size_t bv_to_intervals(const bitvec_t* bv, uint32_t* out) {
  size_t count = 0;
  bool in = false;
  for (size_t i = 0; i < N; i++) {
    if (bv->bits[i] && !in) {
      out[count * 2] = (uint32_t)i;
      in = true;
    } else if (!bv->bits[i] && in) {
      out[count * 2 + 1] = (uint32_t)i;
      count++;
      in = false;
    }
  }
  if (in) {
    out[count * 2 + 1] = N;
    count++;
  }
  return count;
}

static void test_exhaustive_range(void) {
  bitvec_t bx;

  // Verify insert_range and remove_range match interval versions
  // for all 2^8 x 2^8 combinations
  for (unsigned xm = 0; xm < (1u << N); xm++) {
    bv_clear(&bx);
    uint32_t xr[2 * N];
    for (size_t i = 0; i < N; i++)
      if (xm & (1u << i)) bx.bits[i] = 1;
    size_t xcount = bv_to_intervals(&bx, xr);

    // Test insert_range with various ranges
    ucprange_t test_ranges[] = {
      ucprange(0, 0), ucprange(0, 1), ucprange(0, N),
      ucprange(1, 3), ucprange(2, 6), ucprange(N-1, N),
      ucprange(3, 3), ucprange(0, N+1),
    };

    for (size_t ri = 0; ri < sizeof(test_ranges) / sizeof(test_ranges[0]); ri++) {
      ucprange_t r = test_ranges[ri];
      uint32_t a[2 * (N + 1)], b[2 * (N + 1)];
      memcpy(a, xr, xcount * 2 * sizeof(uint32_t));
      memcpy(b, xr, xcount * 2 * sizeof(uint32_t));

      size_t na = ucpset_insert_interval(a, xcount, r.lo, r.hi);
      size_t nb = ucpset_insert_range(b, xcount, r);

      ASSERT(na == nb && memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0,
             "exhaustive insert_range xm=%u ri=%zu", xm, ri);
    }

    for (size_t ri = 0; ri < sizeof(test_ranges) / sizeof(test_ranges[0]); ri++) {
      ucprange_t r = test_ranges[ri];
      uint32_t a[2 * (N + 1)], b[2 * (N + 1)];
      memcpy(a, xr, xcount * 2 * sizeof(uint32_t));
      memcpy(b, xr, xcount * 2 * sizeof(uint32_t));

      size_t na = ucpset_remove_interval(a, xcount, r.lo, r.hi);
      size_t nb = ucpset_remove_range(b, xcount, r);

      ASSERT(na == nb && memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0,
             "exhaustive remove_range xm=%u ri=%zu", xm, ri);
    }

    // Test contains_range
    for (size_t ri = 0; ri < sizeof(test_ranges) / sizeof(test_ranges[0]); ri++) {
      ucprange_t r = test_ranges[ri];
      bool a = ucpset_contains_interval(xr, xcount, r.lo, r.hi);
      bool b = ucpset_contains_range(xr, xcount, r);
      ASSERT(a == b, "exhaustive contains_range xm=%u ri=%zu", xm, ri);
    }
  }
}

// ================== _range forms of the interval operations ================

static void test_range_ops(void) {
  uint32_t x[] = {10, 20, 30, 40, 50, 60};
  size_t n = 3;
  ucprange_t ranges[] = {
    {0, 5}, {5, 15}, {15, 35}, {20, 30}, {25, 28}, {0, 100},
    {40, 50}, {55, 70}, {70, 80}, {12, 12}, {30, 20},
  };

  for (size_t r = 0; r < sizeof(ranges) / sizeof(ranges[0]); r++) {
    uint32_t a[16], b[16];
    size_t i1, j1, i2, j2;

    ucpset_find_overlap_interval(x, n, ranges[r].lo, ranges[r].hi, &i1, &j1);
    ucpset_find_overlap_range(x, n, ranges[r], &i2, &j2);
    ASSERT(i1 == i2 && j1 == j2, "find_overlap_range r=%zu", r);

    ucpset_find_coalesce_interval(x, n, ranges[r].lo, ranges[r].hi, &i1, &j1);
    ucpset_find_coalesce_range(x, n, ranges[r], &i2, &j2);
    ASSERT(i1 == i2 && j1 == j2, "find_coalesce_range r=%zu", r);

    size_t na = ucpset_union_interval(a, x, n, ranges[r].lo, ranges[r].hi);
    size_t nb = ucpset_union_range(b, x, n, ranges[r]);
    ASSERT(na == nb && memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0, "union_range r=%zu", r);

    na = ucpset_intersection_interval(a, x, n, ranges[r].lo, ranges[r].hi);
    nb = ucpset_intersection_range(b, x, n, ranges[r]);
    ASSERT(na == nb && memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0, "intersection_range r=%zu", r);

    na = ucpset_difference_interval(a, x, n, ranges[r].lo, ranges[r].hi);
    nb = ucpset_difference_range(b, x, n, ranges[r]);
    ASSERT(na == nb && memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0, "difference_range r=%zu", r);

    na = ucpset_xor_interval(a, x, n, ranges[r].lo, ranges[r].hi);
    nb = ucpset_xor_range(b, x, n, ranges[r]);
    ASSERT(na == nb && memcmp(a, b, na * 2 * sizeof(uint32_t)) == 0, "xor_range r=%zu", r);
  }
}

// ================================== Main ===================================

int main(void) {
  printf("Running ucprange basics...\n");
  test_ucprange_basics();
  printf(" ucprange basics OK\n");

  printf("Running insert_range tests...\n");
  test_insert_range();
  printf(" insert_range OK\n");

  printf("Running remove_range tests...\n");
  test_remove_range();
  printf(" remove_range OK\n");

  printf("Running contains_range tests...\n");
  test_contains_range();
  printf(" contains_range OK\n");

  printf("Running exhaustive range tests (N=%d)...\n", N);
  test_exhaustive_range();
  printf(" exhaustive range OK\n");

  printf("Running _range operation tests...\n");
  test_range_ops();
  printf(" _range operations OK\n");

  printf("\n All ucprange tests passed!\n");
  return 0;
}
