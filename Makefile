# qnx-rt-probe — Makefile (host-only build; QNX path stubbed)
# C11, POSIX + pthreads, no external deps.

CC       := gcc
CFLAGS   := -std=c11 -Wall -Wextra -O2 -pthread
TARGET   := qrtp

SRCDIR   := src
TESTDIR  := tests

SRCS     := $(SRCDIR)/main.c $(SRCDIR)/bench.c $(SRCDIR)/stats.c $(SRCDIR)/report.c $(SRCDIR)/transport_posix.c
OBJS     := $(SRCS:.c=.o)

.PHONY: all clean test qnx

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

TEST_STATS_BIN := build/test_stats

.PHONY: test_stats
test_stats: $(TEST_STATS_BIN)
	./$(TEST_STATS_BIN)

$(TEST_STATS_BIN): $(TESTDIR)/test_stats.c $(SRCDIR)/stats.c $(SRCDIR)/stats.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Isrc -o $@ $(TESTDIR)/test_stats.c $(SRCDIR)/stats.c -lm

.PHONY: test_smoke
test_smoke: all
	bash tests/test_bench.sh

test: test_stats test_smoke

qnx:
	@echo 'QNX build requires qcc (QNX SDP 8.0). It is NOT installed on this host.'
	@echo 'When qcc is available, the next step is to cross-compile src/qnx_demo.c for ntox86_64.'
	@if command -v qcc >/dev/null 2>&1; then \
		echo 'qcc found — QNX cross-compile path not yet implemented in this stub.'; \
		exit 1; \
	else \
		echo 'Missing: qcc (QNX SDP 8.0). See qrtp build for details.'; \
		exit 1; \
	fi

clean:
	rm -f $(OBJS)
	rm -f $(TARGET)
	rm -rf build
	rm -rf results
