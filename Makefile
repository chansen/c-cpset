CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?=

HEADERS = ucprange.h \
	  ucprange_set.h \
	  ucpset.h \
	  ucpset_canonical.h \
	  ucpset_cardinality.h \
	  ucpset_inplace.h \
	  ucpset_insert.h \
	  ucpset_internal.h \
	  ucpset_membership.h \
	  ucpset_merge.h \
	  ucpset_predicate.h \
	  ucpset_remove.h \
	  ucpset_search.h \
	  ucpset_slice.h \
	  ucpset_splice.h \
	  ucpset_sweep.h

TESTS   = test_ucpset test_ucprange test_ucpset_gallop

.PHONY: all test bench clean asan ubsan debug cover report

all: $(TESTS)

test_ucpset: test_ucpset.c $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ test_ucpset.c

test_ucprange: test_ucprange.c $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ test_ucprange.c

# The same suite with galloping forced on every step, so the tiny sets of
# the exhaustive tests reach the gallop paths too.
test_ucpset_gallop: test_ucpset.c $(HEADERS)
	$(CC) $(CFLAGS) -DUCPSET_GALLOP_MIN=1 $(LDFLAGS) -o $@ test_ucpset.c

test: $(TESTS)
	./test_ucpset
	./test_ucprange
	./test_ucpset_gallop

# Timings for the whole library; see the Benchmark section of the README.
# bench/unicode_sets.h is generated: perl bench/gen_unicode_sets.pl
bench/bench: bench/bench.c bench/unicode_sets.h $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) -I. -o $@ bench/bench.c

bench: bench/bench
	./bench/bench

debug:
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O0 -g -o test_ucpset test_ucpset.c
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O0 -g -o test_ucprange test_ucprange.c
	./test_ucpset
	./test_ucprange

asan:
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O1 -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o test_ucpset test_ucpset.c
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O1 -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o test_ucprange test_ucprange.c
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O1 -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-DUCPSET_GALLOP_MIN=1 \
		-o test_ucpset_gallop test_ucpset.c
	./test_ucpset
	./test_ucprange
	./test_ucpset_gallop

ubsan:
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O1 -g \
		-fsanitize=undefined -fno-omit-frame-pointer \
		-o test_ucpset test_ucpset.c
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O1 -g \
		-fsanitize=undefined -fno-omit-frame-pointer \
		-o test_ucprange test_ucprange.c
	./test_ucpset
	./test_ucprange

cover:
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
		--coverage -fprofile-arcs -ftest-coverage \
		-c -o test_ucpset.o test_ucpset.c
	$(CC) --coverage -o test_ucpset test_ucpset.o
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
		--coverage -fprofile-arcs -ftest-coverage \
		-c -o test_ucprange.o test_ucprange.c
	$(CC) --coverage -o test_ucprange test_ucprange.o
	./test_ucpset
	./test_ucprange
	@gcov -b test_ucpset.c test_ucprange.c > /tmp/gcov_out.txt 2>&1
	@cat /tmp/gcov_out.txt
	@echo "---"
	@echo "Per-header coverage:"
	@for f in ucprange.h ucprange_set.h ucpset_*.h; do \
		echo "  $$f:"; \
		grep -A3 "File '$$f'" /tmp/gcov_out.txt | grep -E "Lines|Branches" || echo "    (no data)"; \
	done
	@rm -f /tmp/gcov_out.txt

report:
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
		--coverage -fprofile-arcs -ftest-coverage \
		-c -o test_ucpset.o test_ucpset.c
	$(CC) --coverage -o test_ucpset test_ucpset.o
	$(CC) -std=c99 -Wall -Wextra -Wpedantic -O0 -g \
		--coverage -fprofile-arcs -ftest-coverage \
		-c -o test_ucprange.o test_ucprange.c
	$(CC) --coverage -o test_ucprange test_ucprange.o
	./test_ucpset
	./test_ucprange
	@mkdir -p cover
	gcovr --exclude "test_ucpset.c" --exclude "test_ucprange.c" \
		--html-details -o cover/coverage.html
	@echo "Coverage report: cover/coverage.html"

clean:
	rm -rf $(TESTS) bench/bench *.o *.gcno *.gcda *.gcov *.dSYM cover
