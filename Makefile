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
# The same Makefile drives GCC, Clang and MSVC.  On Windows the libraries are
# named libstl.lib / libstl.dll (MSVC) or libstl.a / libstl.dll (MinGW), and
# Python is used to build the static library because `ar` is not portable.
#
# Useful variables:
#   CC=clang CFLAGS="-O3 -march=native"   override the toolchain
#   STL_DEBUG=1                           build with asserts and invariants
#   STL_THREADS=0                         build without pthread support
#   TOOLCHAIN=msvc                        select the MSVC flag and naming set
#   PYTHON=/path/to/python                interpreter for the archiver fallback

CC       ?= cc
AR       ?= ar
RANLIB   ?= ranlib
PREFIX   ?= /usr/local
PYTHON   ?= python3

# ---------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------
OS       := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(OS),Windows_NT)
  PLATFORM := windows
else ifeq ($(OS),Windows)
  PLATFORM := windows
else ifeq ($(OS),Darwin)
  PLATFORM := darwin
else
  PLATFORM := unix
endif

# MSVC is the default toolchain on Windows unless the caller says otherwise.
ifeq ($(TOOLCHAIN),)
  ifeq ($(PLATFORM),windows)
    TOOLCHAIN := msvc
  else
    TOOLCHAIN := gcc
  endif
endif

# Shared-library extension and static-library name per platform.
ifeq ($(PLATFORM),windows)
  SHARED_EXT := dll
  ifeq ($(TOOLCHAIN),msvc)
    STATIC_NAME := libstl.lib
  else
    STATIC_NAME := libstl.a
  endif
  EXE_EXT := .exe
else ifeq ($(PLATFORM),darwin)
  SHARED_EXT := dylib
  STATIC_NAME := libstl.a
  EXE_EXT :=
else
  SHARED_EXT := so
  STATIC_NAME := libstl.a
  EXE_EXT :=
endif

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
ifeq ($(TOOLCHAIN),msvc)

  # ---- MSVC (cl.exe / lib.exe / link.exe) ----
  CC     ?= cl
  AR     ?= lib
  OPT    ?= /O2
  # /W4 is the closest equivalent of -Wall -Wextra; the CRT-security warnings
  # for the standard functions we use are noise here.
  WARNINGS ?= /W4 /wd4996
  CSTD     ?= /std:c11
  CFLAGS   ?= $(CSTD) $(OPT) $(WARNINGS) /nologo
  CFLAGS   += /I. /Isrc
  # static library objects must not carry STL_BUILD_SHARED
  STATIC_CFLAGS = $(CFLAGS)
  SHARED_DEFINE = /DSTL_BUILD_SHARED
  OBJFLAG  = /Fo$@
  OUTFLAG  = /Fe$@
  LDLIBS   ?=
  PICFLAG  =

else

  # ---- GCC / Clang ----
  OPT      ?= -O2
  CSTD     ?= -std=c11
  WARNINGS ?= -Wall -Wextra -Wshadow -Wcast-align -Wstrict-prototypes \
              -Wmissing-prototypes -Wpointer-arith -Wwrite-strings \
              -Wno-unused-parameter
  CFLAGS   ?= $(CSTD) $(OPT) $(WARNINGS)
  # Public symbols stay visible; internal helpers are stl__*-prefixed, hidden.
  CFLAGS   += -fvisibility=default
  CFLAGS   += -I. -Isrc -fPIC
  STATIC_CFLAGS = $(CFLAGS)
  OBJFLAG  = -o $@
  OUTFLAG  = -o $@
  LDLIBS   ?= -lpthread -lm
  PICFLAG  =
  SHARED_DEFINE = -DSTL_BUILD_SHARED

endif

# Threading can be compiled out entirely; on Windows there is no pthread, and
# the library already falls back to single-threaded behaviour there.
ifeq ($(STL_THREADS),0)
CFLAGS   += -DSTL_DISABLE_THREADS
LDLIBS   := -lm
endif
ifeq ($(PLATFORM),windows)
CFLAGS   += -DSTL_DISABLE_THREADS
LDLIBS   :=
endif

# Debug build: enable asserts and internal invariant checks.
ifdef STL_DEBUG
ifeq ($(TOOLCHAIN),msvc)
CFLAGS   += /Zi /UNDEBUG /DSTL_DEBUG_INVARIANTS
else
CFLAGS   += -g -O0 -UNDEBUG -DSTL_DEBUG_INVARIANTS
endif
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
STATIC   := $(BUILD)/$(STATIC_NAME)
SHARED   := $(BUILD)/libstl.$(SHARED_EXT)

# Versioned shared-library name.  Windows DLLs are not versioned on disk (the
# version lives in the PE header / import library), and macOS conventionally
# keeps only the major version.
# macOS puts the version before the extension (libstl.1.0.0.dylib); ELF puts
# it after (libstl.so.1.0.0).  Windows DLLs carry the version in the PE header
# and are not versioned on disk.
ifeq ($(TOOLCHAIN),msvc)
  SHARED_V   := $(SHARED)
  SHARED_MAJ := $(SHARED)
  IMPORT_LIB := $(BUILD)/libstl_dll.lib
else ifeq ($(PLATFORM),windows)
  SHARED_V   := $(SHARED)
  SHARED_MAJ := $(SHARED)
  IMPORT_LIB := $(BUILD)/liblibstl.dll.a
else ifeq ($(PLATFORM),darwin)
  SHARED_V   := $(BUILD)/libstl.$(VERSION).dylib
  SHARED_MAJ := $(BUILD)/libstl.$(VERSION_MAJOR).dylib
  IMPORT_LIB :=
else
  SHARED_V   := $(SHARED).$(VERSION)
  SHARED_MAJ := $(SHARED).$(VERSION_MAJOR)
  IMPORT_LIB :=
endif

TEST_SRCS := tests/test_libstl.c tests/test_macros.c
TEST_BINS := $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRCS))

# ---------------------------------------------------------------------
# Default target
# ---------------------------------------------------------------------
.PHONY: all
all: $(STATIC) $(SHARED)

# Order-only prerequisite shared by the targets that compile straight from
# source instead of going through the object rules.
$(BUILD):
	@mkdir -p $(BUILD)

# ---------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------
$(BUILD)/%.o: $(SRCDIR)/%.c $(HEADERS) $(SRCDIR)/libstl_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(STATIC_CFLAGS) -c $< $(OBJFLAG)

$(BUILD)/%.lo: $(SRCDIR)/%.c $(HEADERS) $(SRCDIR)/libstl_internal.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SHARED_DEFINE) -c $< $(OBJFLAG)

# ---------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------
# The static library is assembled with a small Python script rather than `ar`,
# so the same rule works with MSVC, MinGW and GNU toolchains.  macOS is the
# exception: its linker expects the BSD __.SYMDEF index and rejects the ELF
# "/" member, so the platform libtool is used there.
ifeq ($(PLATFORM),darwin)
$(STATIC): $(LIB_OBJS)
	@mkdir -p $(BUILD)
	libtool -static -o $@ $(LIB_OBJS)
	@echo "  [static] $@"
else
$(STATIC): $(LIB_OBJS) tools/make_archive.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/make_archive.py $@ $(LIB_OBJS)
	@echo "  [static] $@"
endif

# The shared library is built with a versioned soname and a matching symlink,
# so both `-lstl` at link time and `libstl.so.1` at run time resolve.
ifeq ($(TOOLCHAIN),msvc)
$(SHARED): $(LIB_PICS)
	@mkdir -p $(BUILD)
	link /DLL /OUT:$@ /IMPLIB:$(IMPORT_LIB) $^ $(LDLIBS)
	@echo "  [shared] $@"
else ifeq ($(PLATFORM),darwin)
$(SHARED): $(LIB_PICS)
	@mkdir -p $(BUILD)
	$(CC) -dynamiclib -o $(SHARED_V) $^ $(LDLIBS) \
	    -install_name @rpath/libstl.$(VERSION_MAJOR).dylib \
	    -compatibility_version $(VERSION_MAJOR) \
	    -current_version $(VERSION)
	cd $(BUILD) && ln -sf libstl.$(VERSION).dylib libstl.$(VERSION_MAJOR).dylib \
	              && ln -sf libstl.$(VERSION).dylib libstl.dylib
	@echo "  [shared] $(SHARED_V)"
else ifeq ($(PLATFORM),windows)
# MinGW's driver exports every symbol by default, which would leak the
# stl__* internals.  --exclude-all-symbols turns that off so the export table
# is exactly the set decorated with STL_API (__declspec(dllexport)).
$(SHARED): $(LIB_PICS)
	@mkdir -p $(BUILD)
	$(CC) -shared -o $@ $^ $(LDLIBS) \
	    -Wl,--out-implib,$(IMPORT_LIB) \
	    -Wl,--exclude-all-symbols \
	    -Wl,--enable-auto-import
	@echo "  [shared] $@"
else
$(SHARED): $(LIB_PICS)
	@mkdir -p $(BUILD)
	$(CC) -shared -o $(SHARED_V) $^ $(LDLIBS) \
	    -Wl,-soname,libstl.so.$(VERSION_MAJOR)
	cd $(BUILD) && ln -sf libstl.so.$(VERSION) libstl.so.$(VERSION_MAJOR) \
	              && ln -sf libstl.so.$(VERSION) libstl.so
	@echo "  [shared] $(SHARED_V)"
endif

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
asan: | $(BUILD)
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
test-cleanup: | $(BUILD)
	$(CC) $(CSTD) $(OPT) $(WARNINGS) -DSTL_ENABLE_CLEANUP -I. -Isrc \
	    -o $(BUILD)/cleanup_test tests/test_macros.c $(LIB_SRCS) $(LDLIBS)
	./$(BUILD)/cleanup_test

# Build the whole suite with pthread support compiled out, which exercises the
# single-threaded fallbacks every non-POSIX platform relies on.
.PHONY: test-nothreads
test-nothreads: | $(BUILD)
	$(CC) $(CSTD) $(OPT) $(WARNINGS) -DSTL_DISABLE_THREADS -I. -Isrc \
	    -o $(BUILD)/nothreads_test tests/test_libstl.c $(LIB_SRCS) -lm
	$(BUILD)/nothreads_test

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
	    ln -sf $(notdir $(SHARED_V)) $(notdir $(SHARED_MAJ)) && \
	    ln -sf $(notdir $(SHARED_V)) libstl.$(SHARED_EXT)
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
	@echo "  test-nothreads run the tests with pthread support compiled out"
	@echo "  valgrind   run the tests under valgrind"
	@echo "  install    install into PREFIX=$(PREFIX)"
	@echo "  clean      remove build products"
