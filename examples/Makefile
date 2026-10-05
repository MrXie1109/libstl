# Build and run the libstl examples.
#
#   make            build every example into build/
#   make run        build, then run them all
#   make 03         build just examples/03_algorithms.c
#   make asan       build and run everything under ASan + UBSan
#   make clean      remove the example binaries
#
# The examples link against the library in ../build, so build it first:
#
#   cd .. && make
#
# They use the C11 API only; STL_ENABLE_CLEANUP, which needs a compiler
# extension, is exercised by examples/05_macros.c in a separate target below.

CC       ?= cc
CSTD     ?= -std=c11
WARNINGS ?= -Wall -Wextra
OPT      ?= -O2

ROOT     := ..
BUILD    := build
LIB      := $(ROOT)/build/libstl.a
LDLIBS   ?= -lpthread -lm

CFLAGS   := $(CSTD) $(OPT) $(WARNINGS) -I$(ROOT)

SOURCES  := $(wildcard *.c)
BINARIES := $(patsubst %.c,$(BUILD)/%,$(SOURCES))

# The macro example is also built once with the optional scope-exit helpers.
CLEANUP_BIN := $(BUILD)/05_macros_cleanup

.PHONY: all run asan clean help
all: $(BINARIES)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%: %.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LDLIBS)

# Build the library if it is missing, so a fresh checkout works in one step.
$(LIB):
	@$(MAKE) -C $(ROOT) all

# `make 03` builds examples/03_algorithms.c
.PHONY: 01 02 03 04 05 06
01 02 03 04 05 06: %: $(BUILD)/%

.PHONY: run
run: all
	@for bin in $(BINARIES); do \
	    echo "=== $$bin ==="; \
	    ./$$bin || exit 1; \
	    echo; \
	done

# The scope-exit helpers rely on the cleanup attribute, so they are opt-in and
# need gnu11 rather than strict c11.
.PHONY: cleanup
cleanup: | $(BUILD)
	$(CC) -std=gnu11 $(OPT) $(WARNINGS) -DSTL_ENABLE_CLEANUP -I$(ROOT) \
	    -o $(CLEANUP_BIN) 05_macros.c $(LIB) $(LDLIBS)
	./$(CLEANUP_BIN)

.PHONY: asan
asan:
	@mkdir -p $(BUILD)
	@for src in $(SOURCES); do \
	    echo "=== $$src ==="; \
	    $(CC) $(CSTD) -g -O1 $(WARNINGS) -I$(ROOT) \
	        -fsanitize=address,undefined -fno-omit-frame-pointer \
	        -o $(BUILD)/asan_$${src%.c} $$src $(ROOT)/src/*.c $(LDLIBS) || exit 1; \
	    ./$(BUILD)/asan_$${src%.c} > /dev/null || exit 1; \
	    echo "  ok"; \
	done
	@echo
	@echo "all examples clean under ASan + UBSan"

.PHONY: clean
clean:
	rm -rf $(BUILD)

.PHONY: help
help:
	@echo "targets: all run asan cleanup clean"
	@echo "        01 02 03 04 05 06   build one example by number"
