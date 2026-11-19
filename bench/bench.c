// Benchmark for ucpset.
//
//   make bench                 (from the project directory)
//   ./bench/bench [section]    lookups, properties, random, queries, editing
//
// Each figure is the fastest of BENCH_REPS timed batches, and a batch
// repeats the operation until it has run for at least BENCH_BATCH_NS.
// Every table has one unit, named in its heading.
#define _POSIX_C_SOURCE 200112L  // clock_gettime, and snprintf under -std=c99
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ucpset.h"
#include "unicode_sets.h"

#ifndef BENCH_BATCH_NS
#define BENCH_BATCH_NS 2e6
#endif
#ifndef BENCH_REPS
#define BENCH_REPS 9
#endif

static void die(const char* what) {
  fprintf(stderr, "bench: %s\n", what);
  exit(1);
}

// ---------------------------------------------------------------- timing

static double now_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

// Results go here so the work that produced them is not optimised away.
static volatile size_t sink;

// Returns p in a way the compiler cannot see through.  Without it a call
// whose arguments never change could be hoisted out of the timing loop.
static const void* volatile opaque_slot;
static const uint32_t* opaque(const uint32_t* p) {
  opaque_slot = p;
  return (const uint32_t*)opaque_slot;
}

// Sets `ns` to the time one execution of `stmt` takes, in nanoseconds.
// For operations that leave their input as it was.
#define MEASURE(ns, stmt)                                    \
  do {                                                       \
    size_t iters_ = 1;                                       \
    double best_ = 0;                                        \
    for (int rep_ = 0; rep_ < BENCH_REPS;) {                 \
      double start_ = now_ns();                              \
      for (size_t it_ = 0; it_ < iters_; it_++) {            \
        stmt;                                                \
      }                                                      \
      double took_ = now_ns() - start_;                      \
      if (rep_ == 0 && took_ < BENCH_BATCH_NS) {             \
        iters_ *= 2; /* still finding the batch size */      \
        continue;                                            \
      }                                                      \
      if (rep_ == 0 || took_ < best_)                        \
        best_ = took_;                                       \
      rep_++;                                                \
    }                                                        \
    (ns) = best_ / (double)iters_;                           \
  } while (0)

// What a pair of now_ns() calls adds to the region between them.
static double clock_cost;

static void measure_clock_cost(void) {
  enum { PAIRS = 200000 };
  double total = 0;
  for (int i = 0; i < PAIRS; i++) {
    double start = now_ns();
    total += now_ns() - start;
  }
  clock_cost = total / PAIRS;
}

// Buffers for operations that overwrite their input.
enum { POOL_ELEMS = 1 << 17, POOL_TARGET = 1 << 16, POOL_COPIES_MAX = 64 };
static uint32_t pool[POOL_ELEMS];

typedef size_t (*overwrite_fn)(uint32_t* buf, const void* arg);

// The time one call of `run` takes, in nanoseconds, for an operation that
// overwrites its input.  Each call gets a fresh copy of `src` in a buffer
// of buf_elems elements.  Only the calls are timed: the copies are made
// before the clock starts, several at a time so that the timed region is
// long next to the cost of reading the clock.
static double time_overwriting(const uint32_t* src, size_t src_elems, size_t buf_elems,
                               overwrite_fn run, const void* arg) {
  if (src_elems > buf_elems || buf_elems > POOL_ELEMS)
    die("the buffer pool is too small");
  size_t copies = POOL_TARGET / buf_elems;
  if (copies < 1)
    copies = 1;
  if (copies > POOL_COPIES_MAX)
    copies = POOL_COPIES_MAX;

  double best = 0;
  for (int rep = 0; rep < BENCH_REPS; rep++) {
    double timed = 0;
    size_t calls = 0;
    while (timed < BENCH_BATCH_NS) {
      for (size_t c = 0; c < copies; c++)
        memcpy(pool + c * buf_elems, src, src_elems * sizeof(uint32_t));
      double start = now_ns();
      for (size_t c = 0; c < copies; c++)
        sink += run(pool + c * buf_elems, arg);
      timed += now_ns() - start;
      calls += copies;
    }
    double each = (timed - clock_cost * (double)(calls / copies)) / (double)calls;
    if (rep == 0 || each < best)
      best = each;
  }
  return best;
}

// ------------------------------------------------------------------ data

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return (uint32_t)(rng_state >> 16);
}

typedef struct {
  const char* name;
  const uint32_t* set;
  size_t count;
} set_t;

static set_t property(const char* name) {
  for (size_t i = 0; i < UNICODE_PROPERTIES_COUNT; i++)
    if (strcmp(unicode_properties[i].name, name) == 0)
      return (set_t){name, unicode_properties[i].set, unicode_properties[i].count};
  die("unicode_sets.h lacks a property the benchmark uses");
  return (set_t){0};
}

static set_t script(const char* name) {
  for (size_t i = 0; i < UNICODE_SCRIPTS_COUNT; i++)
    if (strcmp(unicode_scripts[i].name, name) == 0)
      return (set_t){name, unicode_scripts[i].set, unicode_scripts[i].count};
  die("unicode_sets.h lacks a script the benchmark uses");
  return (set_t){0};
}

// "Lu (638)"
static const char* label(set_t s) {
  static char text[4][40];
  static int next;
  char* t = text[next++ % 4];
  snprintf(t, 40, "%s (%zu)", s.name, s.count);
  return t;
}

// A random canonical set of n intervals: gaps and lengths are uniform in
// [1, codepoint range / n / 2].
static size_t random_set(uint32_t* x, size_t n) {
  uint32_t avg = (uint32_t)(UCPSET_BOUND_MAX / n / 2);
  uint32_t pos = 0;
  size_t k = 0;
  for (; k < n; k++) {
    pos += 1 + rnd() % avg;
    uint32_t len = 1 + rnd() % avg;
    if (pos + len > UCPSET_BOUND_MAX)
      break;
    x[2 * k] = pos;
    x[2 * k + 1] = pos + len;
    pos += len;
  }
  if (k != n || !ucpset_canonical(x, k))
    die("random_set failed");
  return k;
}

// Generated text.  Real text has structure a uniform random stream lacks:
// runs of one script, broken up by spaces and punctuation.
enum { TEXT_LEN = 65536, TEXT_KINDS = 5 };
static uint32_t text[TEXT_KINDS][TEXT_LEN];
static const char* const text_name[TEXT_KINDS] = {
    "English", "Greek", "CJK", "mixed scripts", "random BMP"};

static void put(uint32_t* t, size_t* n, uint32_t c) {
  if (*n < TEXT_LEN)
    t[(*n)++] = c;
}

// A word of 1 to maxlen codepoints from [first, first + span).
static void word(uint32_t* t, size_t* n, uint32_t first, uint32_t span, uint32_t maxlen) {
  for (uint32_t len = 1 + rnd() % maxlen; len > 0; len--)
    put(t, n, first + rnd() % span);
}

// A space; one time in ten a comma or full stop comes first.
static void space(uint32_t* t, size_t* n) {
  uint32_t r = rnd() % 20;
  if (r < 2)
    put(t, n, r == 0 ? ',' : '.');
  put(t, n, ' ');
}

static void make_text(void) {
  size_t n;

  // English: lowercase words, some capitalised, now and then a number.
  for (n = 0; n < TEXT_LEN;) {
    if (rnd() % 8 == 0)
      put(text[0], &n, 'A' + rnd() % 26);
    word(text[0], &n, 'a', 26, 9);
    space(text[0], &n);
    if (rnd() % 40 == 0) {
      word(text[0], &n, '0', 10, 4);
      space(text[0], &n);
    }
  }

  // Greek: lowercase words with ASCII spaces and punctuation.
  for (n = 0; n < TEXT_LEN;) {
    word(text[1], &n, 0x3B1, 25, 9);
    space(text[1], &n);
  }

  // CJK: runs of ideographs ended by ideographic punctuation, with an
  // occasional Latin word.
  for (n = 0; n < TEXT_LEN;) {
    word(text[2], &n, 0x4E00, 0x5000, 20);
    put(text[2], &n, rnd() % 2 ? 0x3002 : 0x3001);
    if (rnd() % 10 == 0)
      word(text[2], &n, 'a', 26, 6);
  }

  // Mixed: 1 to 30 words in one script, then another script.
  for (n = 0; n < TEXT_LEN;) {
    static const uint32_t first[] = {'a',    0x430,  0x3B1, 0x5D0, 0x627,
                                     0x4E00, 0x3041, 0x905, 0xAC00};
    static const uint32_t span[] = {26, 32, 25, 27, 36, 0x5000, 86, 53, 11172};
    uint32_t s = rnd() % 9;
    for (uint32_t words = 1 + rnd() % 30; words > 0; words--) {
      word(text[3], &n, first[s], span[s], 8);
      space(text[3], &n);
    }
  }

  // Random: uniform over the BMP.  No locality at all.
  for (n = 0; n < TEXT_LEN; n++)
    text[4][n] = rnd() % 0x10000;
}

enum { BIG = 30000, SMALL = 300, NORMALIZE_N = 10000 };
static uint32_t big_a[2 * BIG], big_b[2 * BIG], small_a[2 * SMALL];
static uint32_t dst[2 * (2 * BIG + 1)];
static uint32_t work[2 * (2 * BIG + 1)];

// -------------------------------------------------------------- lookups

static UCPSET_NOINLINE size_t count_contains(const uint32_t* x, size_t xcount,
                                             const uint32_t* t, size_t len) {
  size_t found = 0;
  for (size_t k = 0; k < len; k++)
    found += ucpset_contains(x, xcount, t[k]);
  return found;
}

static UCPSET_NOINLINE size_t count_contains_hint(const uint32_t* x, size_t xcount,
                                                  const uint32_t* t, size_t len) {
  size_t found = 0;
  size_t hint = 0;
  for (size_t k = 0; k < len; k++)
    found += ucpset_contains_hint(x, xcount, t[k], &hint);
  return found;
}

static void bench_lookups(void) {
  const set_t sets[] = {
      property("Alphabetic"), property("L"),  property("Lu"),
      property("Grapheme_Extend"), property("Nd"), script("Latin"),
      property("White_Space"),
  };

  printf("Lookups: ns per codepoint\n");
  printf("ucpset_contains and ucpset_contains_hint over %d codepoints of generated text\n\n",
         (int)TEXT_LEN);
  printf("%-22s", "");
  for (int t = 0; t < TEXT_KINDS; t++)
    printf(t + 1 < TEXT_KINDS ? "  %-15s" : "  %s", text_name[t]);
  printf("\n%-22s", "set (intervals)");
  for (int t = 0; t < TEXT_KINDS; t++)
    printf("  %8s %6s", "contains", "hint");
  printf("\n");

  for (size_t s = 0; s < sizeof sets / sizeof sets[0]; s++) {
    printf("%-22s", label(sets[s]));
    for (int t = 0; t < TEXT_KINDS; t++) {
      double plain, hinted;
      MEASURE(plain, sink += count_contains(opaque(sets[s].set), sets[s].count,
                                            opaque(text[t]), TEXT_LEN));
      MEASURE(hinted, sink += count_contains_hint(opaque(sets[s].set), sets[s].count,
                                                  opaque(text[t]), TEXT_LEN));
      if (count_contains(sets[s].set, sets[s].count, text[t], TEXT_LEN) !=
          count_contains_hint(sets[s].set, sets[s].count, text[t], TEXT_LEN))
        die("contains and contains_hint disagree");
      printf("  %8.1f %6.1f", plain / TEXT_LEN, hinted / TEXT_LEN);
    }
    printf("\n");
  }
  printf("\n");
}

// ------------------------------------------------------- set operations

typedef size_t (*merge_fn)(uint32_t*, const uint32_t*, size_t, const uint32_t*, size_t);
typedef size_t (*inplace_fn)(uint32_t*, size_t, const uint32_t*, size_t);

enum { OPERATIONS = 4, PAIRS_MAX = 8 };
static const struct {
  const char* name;
  merge_fn merge;
  inplace_fn inplace;
} operations[OPERATIONS] = {
    {"union", ucpset_union, ucpset_inplace_union},
    {"intersection", ucpset_intersection, ucpset_inplace_intersection},
    {"difference", ucpset_difference, ucpset_inplace_difference},
    {"xor", ucpset_xor, ucpset_inplace_xor},
};

typedef struct {
  inplace_fn op;
  size_t xcount;
  const uint32_t* y;
  size_t ycount;
} inplace_arg;

static size_t run_inplace(uint32_t* buf, const void* arg) {
  const inplace_arg* a = arg;
  return a->op(buf, a->xcount, a->y, a->ycount);
}

static size_t run_inplace_complement(uint32_t* buf, const void* arg) {
  return ucpset_inplace_complement(buf, *(const size_t*)arg, 0, UCPSET_BOUND_MAX);
}

static void matrix(const char* heading, const set_t (*pairs)[2], size_t pair_count,
                   double (*ns)[OPERATIONS], double unit, int decimals) {
  printf("%s\n", heading);
  printf("%-18s  %-28s", "x (intervals)", "y (intervals)");
  for (int op = 0; op < OPERATIONS; op++)
    printf("  %12s", operations[op].name);
  printf("\n");
  for (size_t p = 0; p < pair_count; p++) {
    printf("%-18s  %-28s", label(pairs[p][0]), label(pairs[p][1]));
    for (int op = 0; op < OPERATIONS; op++)
      printf("  %12.*f", decimals, ns[p][op] / unit);
    printf("\n");
  }
  printf("\n");
}

// Times the four operations on each pair, out-of-place and in-place, and
// the complement of `whole`, and prints them in units of `unit` ns.
static void bench_pairs(const set_t (*pairs)[2], size_t pair_count, set_t whole,
                        double unit, int decimals) {
  static double out[PAIRS_MAX][OPERATIONS], in[PAIRS_MAX][OPERATIONS];
  if (pair_count > PAIRS_MAX)
    die("too many pairs");

  for (size_t p = 0; p < pair_count; p++) {
    const set_t x = pairs[p][0], y = pairs[p][1];
    for (int op = 0; op < OPERATIONS; op++) {
      const merge_fn merge = operations[op].merge;
      const inplace_fn inplace = operations[op].inplace;

      MEASURE(out[p][op],
              sink += merge(dst, opaque(x.set), x.count, opaque(y.set), y.count));

      const inplace_arg arg = {inplace, x.count, y.set, y.count};
      in[p][op] = time_overwriting(x.set, 2 * x.count, 2 * (x.count + y.count),
                                   run_inplace, &arg);

      // Both forms must give the same set.
      size_t n = merge(dst, x.set, x.count, y.set, y.count);
      memcpy(work, x.set, 2 * x.count * sizeof(uint32_t));
      size_t m = inplace(work, x.count, y.set, y.count);
      if (n != m || memcmp(dst, work, 2 * n * sizeof(uint32_t)) != 0 ||
          !ucpset_canonical(dst, n))
        die("out-of-place and in-place results differ");
    }
  }

  matrix("out-of-place: dst = x op y", pairs, pair_count, out, unit, decimals);
  matrix("in-place: x = x op y", pairs, pair_count, in, unit, decimals);

  double out_c, in_c;
  MEASURE(out_c,
          sink += ucpset_complement(dst, opaque(whole.set), whole.count, 0, UCPSET_BOUND_MAX));
  in_c = time_overwriting(whole.set, 2 * whole.count, 2 * (whole.count + 1),
                          run_inplace_complement, &whole.count);
  printf("complement of x over all codepoints\n");
  printf("%-18s  %12s  %12s\n", "x (intervals)", "out-of-place", "in-place");
  printf("%-18s  %12.*f  %12.*f\n\n", label(whole), decimals, out_c / unit, decimals,
         in_c / unit);
}

static void bench_properties(void) {
  static const uint32_t ascii[] = {0x0000, 0x0080};
  const set_t pairs[][2] = {
      {property("Lu"), property("Ll")},
      {property("Alphabetic"), property("Grapheme_Extend")},
      {property("L"), property("Nd")},
      {property("L"), script("Greek")},
      {property("Alphabetic"), script("Han")},
      {property("P"), {"ASCII", ascii, 1}},
  };

  printf("Set operations on Unicode properties (Unicode %s): ns per call\n\n",
         UNICODE_SETS_VERSION);
  bench_pairs(pairs, sizeof pairs / sizeof pairs[0], property("L"), 1.0, 1);
}

static void bench_random(void) {
  // One interval wide enough to cover about a hundred of the set's, placed
  // near its start, in its middle and near its end.
  static uint32_t at_start[2], at_middle[2], at_end[2];
  const uint32_t span = big_a[2 * BIG - 1];
  const uint32_t width = span / BIG * 100;
  at_start[0] = span / 100;
  at_middle[0] = span / 2;
  at_end[0] = span - span / 100 - width;
  at_start[1] = at_start[0] + width;
  at_middle[1] = at_middle[0] + width;
  at_end[1] = at_end[0] + width;

  const set_t a = {"random", big_a, BIG};
  const set_t b = {"random", big_b, BIG};
  const set_t s = {"random", small_a, SMALL};
  const set_t pairs[][2] = {
      {a, b},
      {a, s},
      {s, a},
      {a, {"interval near the start", at_start, 1}},
      {a, {"interval in the middle", at_middle, 1}},
      {a, {"interval near the end", at_end, 1}},
  };

  printf("Set operations on large random sets: us per call\n\n");
  bench_pairs(pairs, sizeof pairs / sizeof pairs[0], a, 1e3, 2);
}

// -------------------------------------------------------------- queries

static const uint32_t* script_sets[UNICODE_SCRIPTS_COUNT];
static size_t script_counts[UNICODE_SCRIPTS_COUNT];
static size_t script_scratch[2 * UNICODE_SCRIPTS_COUNT];

// What ucpset_pairwise_disjoint replaces: ucpset_disjoint on every pair.
static bool every_pair_disjoint(const uint32_t* const* sets, const size_t* counts, size_t k) {
  for (size_t a = 0; a < k; a++)
    for (size_t b = a + 1; b < k; b++)
      if (!ucpset_disjoint(sets[a], counts[a], sets[b], counts[b]))
        return false;
  return true;
}

static void row_header(const char* unit) {
  printf("%-68s  %-7s  %10s\n", "call", "result", unit);
}

static void row(const char* what, const char* result, double value, int decimals) {
  printf("%-68s  %-7s  %10.*f\n", what, result, decimals, value);
}

static const char* yes_no(bool b) {
  return b ? "true" : "false";
}

static const char* number(size_t n) {
  static char text[24];
  snprintf(text, sizeof text, "%zu", n);
  return text;
}

static void bench_queries(void) {
  const set_t alpha = property("Alphabetic");
  const set_t gext = property("Grapheme_Extend");
  const set_t l = property("L");
  const set_t lu = property("Lu");
  const set_t ll = property("Ll");
  const set_t nd = property("Nd");
  const set_t greek = script("Greek");
  const set_t latin = script("Latin");

  // Greek letters: a small set that lies wholly inside L.
  static uint32_t greek_letters[2 * 64];
  if (greek.count > 64)
    die("Greek has outgrown its buffer");
  const size_t greek_letters_count =
      ucpset_intersection(greek_letters, greek.set, greek.count, l.set, l.count);

  // A second copy of L, so that equal has to compare the contents.
  static uint32_t l_copy[2 * 1024];
  if (l.count > 1024)
    die("L has outgrown its buffer");
  memcpy(l_copy, l.set, 2 * l.count * sizeof(uint32_t));

  for (size_t i = 0; i < UNICODE_SCRIPTS_COUNT; i++) {
    script_sets[i] = unicode_scripts[i].set;
    script_counts[i] = unicode_scripts[i].count;
  }

  double ns;
  char what[96];
  printf("Queries: ns per call\n\n");
  row_header("ns");

  MEASURE(ns, sink += ucpset_cardinality(opaque(alpha.set), alpha.count));
  snprintf(what, sizeof what, "cardinality(%s)", label(alpha));
  row(what, number(ucpset_cardinality(alpha.set, alpha.count)), ns, 1);

  MEASURE(ns, sink += ucpset_cardinality_intersection(opaque(alpha.set), alpha.count,
                                                      opaque(gext.set), gext.count));
  snprintf(what, sizeof what, "cardinality_intersection(%s, %s)", label(alpha), label(gext));
  row(what, number(ucpset_cardinality_intersection(alpha.set, alpha.count, gext.set,
                                                   gext.count)), ns, 1);

  MEASURE(ns, sink += ucpset_cardinality_intersection(opaque(l.set), l.count,
                                                      opaque(greek.set), greek.count));
  snprintf(what, sizeof what, "cardinality_intersection(%s, %s)", label(l), label(greek));
  row(what, number(ucpset_cardinality_intersection(l.set, l.count, greek.set, greek.count)),
      ns, 1);

  MEASURE(ns, sink += ucpset_canonical(opaque(alpha.set), alpha.count));
  snprintf(what, sizeof what, "canonical(%s)", label(alpha));
  row(what, yes_no(ucpset_canonical(alpha.set, alpha.count)), ns, 1);

  MEASURE(ns, sink += ucpset_equal(opaque(l.set), l.count, opaque(l_copy), l.count));
  snprintf(what, sizeof what, "equal(%s, a copy of it)", label(l));
  row(what, yes_no(ucpset_equal(l.set, l.count, l_copy, l.count)), ns, 1);

  MEASURE(ns, sink += ucpset_subset(opaque(lu.set), lu.count, opaque(l.set), l.count));
  snprintf(what, sizeof what, "subset(%s, %s)", label(lu), label(l));
  row(what, yes_no(ucpset_subset(lu.set, lu.count, l.set, l.count)), ns, 1);

  MEASURE(ns, sink += ucpset_subset(opaque(greek_letters), greek_letters_count,
                                    opaque(l.set), l.count));
  snprintf(what, sizeof what, "subset(Greek letters (%zu), %s)", greek_letters_count, label(l));
  row(what, yes_no(ucpset_subset(greek_letters, greek_letters_count, l.set, l.count)), ns, 1);

  MEASURE(ns, sink += ucpset_subset(opaque(nd.set), nd.count, opaque(l.set), l.count));
  snprintf(what, sizeof what, "subset(%s, %s)", label(nd), label(l));
  row(what, yes_no(ucpset_subset(nd.set, nd.count, l.set, l.count)), ns, 1);

  MEASURE(ns, sink += ucpset_disjoint(opaque(lu.set), lu.count, opaque(ll.set), ll.count));
  snprintf(what, sizeof what, "disjoint(%s, %s)", label(lu), label(ll));
  row(what, yes_no(ucpset_disjoint(lu.set, lu.count, ll.set, ll.count)), ns, 1);

  MEASURE(ns, sink += ucpset_disjoint(opaque(l.set), l.count, opaque(nd.set), nd.count));
  snprintf(what, sizeof what, "disjoint(%s, %s)", label(l), label(nd));
  row(what, yes_no(ucpset_disjoint(l.set, l.count, nd.set, nd.count)), ns, 1);

  MEASURE(ns, sink += ucpset_disjoint(opaque(latin.set), latin.count,
                                      opaque(greek.set), greek.count));
  snprintf(what, sizeof what, "disjoint(%s, %s)", label(latin), label(greek));
  row(what, yes_no(ucpset_disjoint(latin.set, latin.count, greek.set, greek.count)), ns, 1);

  MEASURE(ns, sink += ucpset_disjoint(opaque(l.set), l.count, opaque(alpha.set), alpha.count));
  snprintf(what, sizeof what, "disjoint(%s, %s)", label(l), label(alpha));
  row(what, yes_no(ucpset_disjoint(l.set, l.count, alpha.set, alpha.count)), ns, 1);

  MEASURE(ns, script_sets[0] = opaque(unicode_scripts[0].set);
              sink += ucpset_pairwise_disjoint(script_sets, script_counts,
                                               UNICODE_SCRIPTS_COUNT, script_scratch));
  snprintf(what, sizeof what, "pairwise_disjoint(all %d scripts)", (int)UNICODE_SCRIPTS_COUNT);
  row(what, yes_no(ucpset_pairwise_disjoint(script_sets, script_counts, UNICODE_SCRIPTS_COUNT,
                                            script_scratch)), ns, 1);

  MEASURE(ns, script_sets[0] = opaque(unicode_scripts[0].set);
              sink += every_pair_disjoint(script_sets, script_counts, UNICODE_SCRIPTS_COUNT));
  snprintf(what, sizeof what, "  the same with disjoint on each of the %d pairs",
           (int)(UNICODE_SCRIPTS_COUNT * (UNICODE_SCRIPTS_COUNT - 1) / 2));
  row(what, yes_no(every_pair_disjoint(script_sets, script_counts, UNICODE_SCRIPTS_COUNT)),
      ns, 1);
  printf("\n");
}

// -------------------------------------------------------------- editing

// A codepoint in the gap after interval i (or a later one) with neither
// neighbour in the set, so that inserting it adds an interval.
static uint32_t lone_codepoint_after(const uint32_t* x, size_t xcount, size_t i) {
  for (; i + 1 < xcount; i++)
    if (x[2 * i + 2] - x[2 * i + 1] >= 3)
      return x[2 * i + 1] + 1;
  return x[2 * xcount - 1] + 1;  // past the last interval
}

// Inserting v adds an interval and removing it restores the set, so the
// pair can be repeated on one buffer.
static void bench_insert_remove(set_t x, const char* where, size_t i) {
  const uint32_t v = lone_codepoint_after(x.set, x.count, i);
  const size_t bytes = 2 * x.count * sizeof(uint32_t);
  memcpy(work, x.set, bytes);

  double ns;
  MEASURE(ns, size_t n = ucpset_insert(work, x.count, v);
              sink += ucpset_remove(work, n, v));

  if (ucpset_insert(work, x.count, v) != x.count + 1 ||
      ucpset_remove(work, x.count + 1, v) != x.count || memcmp(work, x.set, bytes) != 0)
    die("insert and remove did not round-trip");

  char what[96];
  snprintf(what, sizeof what, "%s, %s", label(x), where);
  row(what, "", ns, 1);
}

static size_t run_normalize(uint32_t* buf, const void* arg) {
  return ucpset_normalize(buf, *(const size_t*)arg);
}

static size_t run_normalize_codepoints(uint32_t* buf, const void* arg) {
  return ucpset_normalize_codepoints(buf, *(const size_t*)arg);
}

static void bench_editing(void) {
  static uint32_t source[2 * NORMALIZE_N], sorted[2 * NORMALIZE_N];
  const set_t alpha = property("Alphabetic");
  const set_t big = {"random", big_a, BIG};
  char what[96];

  // Inserting a new interval moves every interval after it up by one, and
  // removing it moves them back: the cost depends on the position.
  printf("Insert a codepoint as a new interval, then remove it: ns per pair of calls\n\n");
  row_header("ns");
  bench_insert_remove(alpha, "near the start", 0);
  bench_insert_remove(alpha, "in the middle", alpha.count / 2);
  bench_insert_remove(alpha, "past the end", alpha.count);
  bench_insert_remove(big, "near the start", 0);
  bench_insert_remove(big, "in the middle", big.count / 2);
  bench_insert_remove(big, "past the end", big.count);
  printf("\n");

  printf("Normalize: us per call\n\n");
  row_header("us");

  size_t count = NORMALIZE_N;
  for (size_t i = 0; i < NORMALIZE_N; i++) {
    uint32_t lo = rnd() % UCPSET_BOUND_MAX;
    source[2 * i] = lo;
    source[2 * i + 1] = lo + 1 + rnd() % 64;
  }
  double ns = time_overwriting(source, 2 * count, 2 * count, run_normalize, &count);
  memcpy(sorted, source, sizeof source);
  const size_t sorted_count = ucpset_normalize(sorted, count);
  if (!ucpset_canonical(sorted, sorted_count))
    die("normalize result is not canonical");
  snprintf(what, sizeof what, "normalize(%zu random intervals, unsorted)", count);
  row(what, number(sorted_count), ns / 1e3, 2);

  // The same intervals once normalized: already sorted, nothing to merge.
  ns = time_overwriting(sorted, 2 * sorted_count, 2 * sorted_count, run_normalize,
                        &sorted_count);
  snprintf(what, sizeof what, "normalize(%zu intervals, already canonical)", sorted_count);
  row(what, number(sorted_count), ns / 1e3, 2);

  for (size_t i = 0; i < NORMALIZE_N; i++)
    source[i] = text[3][rnd() % TEXT_LEN];
  ns = time_overwriting(source, count, 2 * count, run_normalize_codepoints, &count);
  memcpy(sorted, source, count * sizeof(uint32_t));
  const size_t distinct = ucpset_normalize_codepoints(sorted, count);
  if (!ucpset_canonical(sorted, distinct))
    die("normalize_codepoints result is not canonical");
  snprintf(what, sizeof what, "normalize_codepoints(%zu codepoints of mixed-script text)",
           count);
  row(what, number(distinct), ns / 1e3, 2);
  printf("\n");
}

// ----------------------------------------------------------------- main

int main(int argc, char** argv) {
  static const struct {
    const char* name;
    void (*run)(void);
  } sections[] = {
      {"lookups", bench_lookups},     {"properties", bench_properties},
      {"random", bench_random},       {"queries", bench_queries},
      {"editing", bench_editing},
  };
  const size_t section_count = sizeof sections / sizeof sections[0];

  if (argc > 2)
    die("usage: bench [section]");
  if (argc == 2) {
    bool known = false;
    for (size_t s = 0; s < section_count; s++)
      known = known || strcmp(argv[1], sections[s].name) == 0;
    if (!known)
      die("sections: lookups, properties, random, queries, editing");
  }

  for (size_t i = 0; i < UNICODE_PROPERTIES_COUNT; i++)
    if (!ucpset_canonical(unicode_properties[i].set, unicode_properties[i].count))
      die("a property in unicode_sets.h is not canonical");
  for (size_t i = 0; i < UNICODE_SCRIPTS_COUNT; i++)
    if (!ucpset_canonical(unicode_scripts[i].set, unicode_scripts[i].count))
      die("a script in unicode_sets.h is not canonical");

  make_text();
  random_set(big_a, BIG);
  random_set(big_b, BIG);
  random_set(small_a, SMALL);
  measure_clock_cost();

  printf("ucpset benchmark: fastest of %d batches of at least %.0f ms each\n", (int)BENCH_REPS,
         BENCH_BATCH_NS / 1e6);
#if defined(__clang__)
  printf("compiler: %s\n", __VERSION__);
#elif defined(__GNUC__)
  printf("compiler: gcc %s\n", __VERSION__);
#endif
  printf("UCPSET_GALLOP_MIN: %d\n\n", (int)UCPSET_GALLOP_MIN);

  for (size_t s = 0; s < section_count; s++)
    if (argc < 2 || strcmp(argv[1], sections[s].name) == 0)
      sections[s].run();

  return 0;
}
