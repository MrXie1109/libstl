# Makefile for libstl -- an STL implementation for C
#
# Targets:
#   all       build the static and shared libraries (default)
#   test      build and run the self tests
#   check     alias for test
#   asan      build and run the tests under ASan + UBSan
#   shared    with PIC
#   install   copy headers and libraries into $(PREFIX)
#   clean     remove build products
#
# Useful variables:
#   CC=clang CFLAGS="-O3 -march=native"   override the toolchain
#   STL_DEBUG=1                           build with asserts and invariants
#   STL_THREADS=0                         build without pthread support

CC       ?= cc
AR       ?= ar
RANLIB   ?= ranlib
PREFIX   ?= /usr/local

# ---------------------------------------------------------------------
# Version (kept in sync with STL_VERSION_* in libstl.h)
# ---------------------------------------------------------------------
VERSION_MAJOR := 1
VERSION_MINOR := 0
VERSION_PATCH := 0
VERSION       := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)

# ---------------------------------------------------------------------
# Flags
# ---------------------------------------------------------------------
WARNINGS ?= -Wall -Wextra -Wshadow -Wcast-align -Wstrict-prototypes \
            -Wmissing-prototypes -Wpointer-arith -Wwrite-strings \
            -Wno-unused-parameter

OPT      ?= -O2
CSTD     ?= -std=c11

CFLAGS   ?= $(CSTD) $(OPT) $(WARNINGS)
# Public symbols stay visible; internal helpers are stl__*-prefixed and hidden.
CFLAGS   += -fvisibility=default
CFLAGS   += -I. -Isrc -fPIC

LDLIBS   ?= -lpthread -lm

# Threading can be compiled out entirely for freestanding targets.
ifeq ($(STL_THREADS),0)
CFLAGS   += -DSTL_DISABLE_THREADS
LDLIBS   := -lm
endif

# Debug build: enable asserts and internal invariant checks.
ifdef STL_DEBUG
CFLAGS   += -g -O0 -UNDEBUG -DSTL_DEBUG_INVARIANTS
endif

# ---------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------
SRCDIR   := src
INCDIR   := .

# Every generated file lives under $(BUILD); the source tree stays clean.
BUILD    ?= build

LIB_SRCS := $(wildcard $(SRCDIR)/*.c)
LIB_OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.o,$(LIB_SRCS))
LIB_PICS := $(patsubst $(SRCDIR)/%.c,$(BUILD)/%.lo,$(LIB_SRCS))

HEADERS  := libstl.h
STATIC   := $(BUILD)/libstl.a
SHARED   := $(BUILD)/libstl.so

# Shared-library artefacts (versioned file plus the two symlinks).
SHARED_V := $(SHARED).$(VERSION)
SHARED_SO := $(SHARED).$(VERSION_MAJOR)

TEST_SRCS := tests/test_libstl.c tests/test_macros.c
TEST_BINS := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRCS))

# ---------------------------------------------------------------------
# Default target
# ---------------------------------------------------------------------
.PHONY: all
all: $(STATIC) $(SHARED)

# ---------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------
$(BUILD)/%.o: $(SRCDIR)/%.c $(HEADERS) $(SRCDIR)/libstl_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.lo: $(SRCDIR)/%.c $(HEADERS) $(SRCDIR)/libstl_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -DSTL_BUILD_SHARED -c $< -o $@

# ---------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------
$(STATIC): $(LIB_OBJS)
	@mkdir -p $(BUILD)
	$(AR) rcs $@ $^
	$(RANLIB) $@
	@echo "  [static] $@"

# The shared library is built with a versioned soname and a matching symlink,
# so both `-lstl` at link time and `libstl.so.1` at run time resolve.
$(SHARED): $(LIB_PICS)
	@mkdir -p $(BUILD)
	$(CC) -shared -o $(SHARED_V) $^ $(LDLIBS) \
	    -Wl,-soname,libstl.so.$(VERSION_MAJOR)
	cd $(BUILD) && ln -sf libstl.so.$(VERSION) libstl.so.$(VERSION_MAJOR) \
	              && ln -sf libstl.so.$(VERSION) libstl.so
	@echo "  [shared] $(SHARED_V)"

# ---------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------
.PHONY: test check
test check: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	    echo "=== running $$t ==="; \
	    $$t || exit 1; \
	done
	@echo
	@echo "All test binaries passed."

$(BUILD)/%: tests/%.c $(STATIC) $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(STATIC) $(LDLIBS)

.PHONY: asan
asan:
	$(CC) $(CSTD) -g -O1 $(WARNINGS) -I. -Isrc \
	    -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $(BUILD)/asan_test tests/test_libstl.c $(LIB_SRCS) $(LDLIBS)
	$(CC) $(CSTD) -g -O1 $(WARNINGS) -I. -Isrc \
	    -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $(BUILD)/asan_macros tests/test_macros.c $(LIB_SRCS) $(LDLIBS)
	./$(BUILD)/asan_test
	./$(BUILD)/asan_macros

# The scope-exit cleanup helpers need a GCC/Clang extension and are opt-in, so
# they get their own pass with the switch enabled.
.PHONY: test-cleanup
test-cleanup:
	$(CC) $(CSTD) $(OPT) $(WARNINGS) -DSTL_ENABLE_CLEANUP -I. -Isrc \
	    -o $(BUILD)/cleanup_test tests/test_macros.c $(LIB_SRCS) $(LDLIBS)
	./$(BUILD)/cleanup_test

.PHONY: valgrind
valgrind: $(TEST_BINS)
	@for t in $(TEST_BINS); do \
	    echo "=== valgrind $$t ==="; \
	    valgrind --error-exitcode=1 --leak-check=full -q $$t || exit 1; \
	done

# ---------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------
.PHONY: install
install: all
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(HEADERS) $(DESTDIR)$(PREFIX)/include/
	install -m 644 $(STATIC)  $(DESTDIR)$(PREFIX)/lib/
	install -m 755 $(SHARED_V) $(DESTDIR)$(PREFIX)/lib/
	cd $(DESTDIR)$(PREFIX)/lib && \
	    ln -sf libstl.so.$(VERSION) libstl.so.$(VERSION_MAJOR) && \
	    ln -sf libstl.so.$(VERSION) libstl.so
	@echo "installed libstl $(VERSION) into $(DESTDIR)$(PREFIX)"

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/include/libstl.h
	rm -f $(DESTDIR)$(PREFIX)/lib/libstl.a
	rm -f $(DESTDIR)$(PREFIX)/lib/libstl.so
	rm -f $(DESTDIR)$(PREFIX)/lib/libstl.so.$(VERSION)
	rm -f $(DESTDIR)$(PREFIX)/lib/libstl.so.$(VERSION_MAJOR)

# ---------------------------------------------------------------------
# Housekeeping
# ---------------------------------------------------------------------
.PHONY: clean distclean
clean:
	rm -rf $(BUILD)

distclean: clean

.PHONY: help
help:
	@echo "libstl $(VERSION) -- make targets:"
	@echo "  all        build $(STATIC) and $(SHARED) (default)"
	@echo "  test       build and run the self tests"
	@echo "  asan       run the tests under AddressSanitizer + UBSan"
	@echo "  test-cleanup  run the macro tests with STL_ENABLE_CLEANUP"
	@echo "  valgrind   run the tests under valgrind"
	@echo "  install    install into PREFIX=$(PREFIX)"
	@echo "  clean      remove build products"
