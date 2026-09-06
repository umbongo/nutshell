# Makefile for Nutshell
#
# Supported build hosts:
#   Linux  — cross-compile with x86_64-w64-mingw32-gcc and vcpkg static libs.
#            Run: make clean && make release
#   Windows (MSYS2 MINGW64) — build natively with pacman-installed libs:
#            pacman -S mingw-w64-x86_64-make mingw-w64-x86_64-libssh2 \
#                      mingw-w64-x86_64-openssl mingw-w64-x86_64-zlib mingw-w64-x86_64-upx
#            Run: mingw32-make clean && mingw32-make release
#            (use mingw32-make, not the MSYS `make`, which mangles gcc's temp path)
#
# The `test` target builds and runs the native unit tests on either host.

ifeq ($(OS),Windows_NT)
HOST_WINDOWS = yes
endif

CC = x86_64-w64-mingw32-gcc

ifeq ($(HOST_WINDOWS),yes)
WINDRES = windres
else
WINDRES = x86_64-w64-mingw32-windres
endif

ifeq ($(HOST_WINDOWS),yes)
# MSYS2 host: gcc's default search paths already cover the pacman-installed
# libssh2 / OpenSSL / zlib, so no extra -I/-L is needed. Link statically so the
# resulting exe does not depend on the MSYS2 DLLs.
DEP_INC   =
DEP_LIB   =
ZLIB      = -lz
LINK_MODE = -static
EXE       = .exe
else
VCPKG_LIB = $(HOME)/vcpkg/installed/x64-mingw-gcc-static/lib
VCPKG_INC = $(HOME)/vcpkg/installed/x64-mingw-gcc-static/include
DEP_INC   = -I$(VCPKG_INC)
DEP_LIB   = -L$(VCPKG_LIB)
ZLIB      = -lzlib
LINK_MODE =
EXE       =
endif

CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion \
         -Os -ffunction-sections -fdata-sections -flto \
         -Isrc -Isrc/core -Isrc/config -Isrc/crypto $(DEP_INC) -Isrc/term -Isrc/ssh -Isrc/ui
LDFLAGS = -mwindows -Os -flto -Wl,--gc-sections -s $(LINK_MODE) \
          $(DEP_LIB) -lssh2 -lssl -lcrypto $(ZLIB) -lcrypt32 -lbcrypt \
          -lgdiplus -lole32 -lshlwapi -lshell32 -lws2_32 -lgdi32 -luser32 -lcomctl32 -ldwmapi -lwinhttp -lm

# Source directories
SRC_DIRS = src src/core src/config src/crypto src/term src/ssh src/ui

# Find all .c files in source directories
SRCS = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))

# Resources
RC_SRCS = src/ui/resource.rc src/ui/nutshell.rc

# Object files
OBJS = $(SRCS:.c=.o) $(RC_SRCS:.rc=.o)

# Build directory and target
BUILD_DIR = build/win
TARGET = $(BUILD_DIR)/nutshell.exe

# Test configuration (native gcc on the build host)
TEST_CC = gcc
TEST_CFLAGS = -std=c11 -Wall -Wextra -g \
              -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui \
              -D_TEST -Wno-unused-function

# Extra system libs needed on Windows: static OpenSSL/libssh2 deps, plus the
# WinHTTP/GDI calls that src/core makes under #ifdef _WIN32.  The 16 MB stack
# matches Linux (Windows defaults to 1 MB; some tests keep large structs on the stack).
ifeq ($(HOST_WINDOWS),yes)
TEST_SYS_LIBS = $(ZLIB) -lcrypt32 -lbcrypt -lws2_32 -lwinhttp -lgdi32 -luser32 -lshlwapi \
                -Wl,--stack,16777216
else
TEST_SYS_LIBS =
endif

# Auto-detect libssh2 availability for test linking.
# (Links to a real file rather than /dev/null: MinGW ld cannot write to /dev/null.)
DETECT_OUT = build/.detect$(EXE)
HAS_LIBSSH2 := $(shell mkdir -p build && echo 'int main(){}' | $(TEST_CC) -xc - -lssh2 -lssl -lcrypto -o $(DETECT_OUT) 2>/dev/null && echo yes || echo no)
# Auto-detect OpenSSL availability (may be present even without libssh2)
HAS_OPENSSL := $(shell mkdir -p build && echo 'int main(){}' | $(TEST_CC) -xc - -lssl -lcrypto -o $(DETECT_OUT) 2>/dev/null && echo yes || echo no)

ifeq ($(HAS_LIBSSH2),yes)
TEST_LDFLAGS = $(LINK_MODE) -lssh2 -lssl -lcrypto $(TEST_SYS_LIBS) -lm
else ifeq ($(HAS_OPENSSL),yes)
TEST_CFLAGS += -DNO_SSH_LIBS -Itests/stubs
TEST_LDFLAGS = $(LINK_MODE) -lssl -lcrypto $(TEST_SYS_LIBS) -lm
else
TEST_CFLAGS += -DNO_SSH_LIBS -Itests/stubs
TEST_LDFLAGS = -lm
endif

# Exclude Windows-only UI files and main entry point from test build
NON_TEST_SRCS = src/main.c $(wildcard src/ui/*.c)

# Win32-only tests belong to the `wintest` harness, never to `test`
WIN_ONLY_TESTS = tests/test_icons.c tests/win_runner.c

# Exclude SSH/knownhosts networking files when libssh2 is unavailable
# (tests then compile against the stub header in tests/stubs/)
ifeq ($(HAS_LIBSSH2),no)
NON_TEST_SRCS += src/term/ssh_session.c src/term/ssh_channel.c src/term/ssh_pty.c \
                 src/config/ssh_io.c src/term/knownhosts.c
NON_TEST_IMPL = $(WIN_ONLY_TESTS) tests/test_ssh.c tests/session.c tests/test_knownhosts.c tests/test_key_auth.c
else
NON_TEST_IMPL = $(WIN_ONLY_TESTS)
endif

# All test implementation files
TEST_IMPL_SRCS = $(filter-out $(NON_TEST_IMPL),$(wildcard tests/*.c))

# All source files, excluding non-test sources
APP_SRCS = $(filter-out $(NON_TEST_SRCS),$(SRCS))

TEST_SRCS = $(APP_SRCS) $(TEST_IMPL_SRCS)
TEST_TARGET = build/test_runner$(EXE)


.PHONY: all clean test lint debug release redraw-debug wintest

# ── Win32-only test harness (icon renderer). Links GDI+ via MinGW.
# Runs directly on a Windows host, under Wine on Linux if available;
# otherwise prints a skip notice.  Kept separate from `test`.
WIN_TEST_CC      = x86_64-w64-mingw32-gcc
WIN_TEST_TARGET  = build/win/test_runner.exe
WIN_TEST_SRCS    = src/ui/icons.c tests/test_icons.c tests/win_runner.c
WIN_TEST_CFLAGS  = -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
                   -Wformat=2 -Wconversion \
                   -Isrc -Isrc/core -Isrc/config -Isrc/crypto \
                   $(DEP_INC) -Isrc/term -Isrc/ssh -Isrc/ui -Itests
WIN_TEST_LDFLAGS = -lgdiplus -lgdi32 -luser32

all: $(TARGET)

release: $(TARGET)
	upx --best --lzma $(TARGET)

# Build with redraw diagnostic logging enabled (writes to OutputDebugString).
# Use DebugView or a debugger to capture output.
redraw-debug:
	$(MAKE) CFLAGS="$(CFLAGS) -DREDRAW_DEBUG"

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.rc
	$(WINDRES) -Isrc/ui $< $@

test:
	@mkdir -p build
	$(TEST_CC) $(TEST_CFLAGS) $(TEST_SRCS) -o $(TEST_TARGET) $(TEST_LDFLAGS)
	./$(TEST_TARGET)

wintest:
	@mkdir -p $(BUILD_DIR)
	$(WIN_TEST_CC) $(WIN_TEST_CFLAGS) $(WIN_TEST_SRCS) -o $(WIN_TEST_TARGET) $(WIN_TEST_LDFLAGS)
ifeq ($(HOST_WINDOWS),yes)
	./$(WIN_TEST_TARGET)
else
	@if command -v wine >/dev/null 2>&1; then \
		wine $(WIN_TEST_TARGET); \
	else \
		echo "[skip] wine not installed — built $(WIN_TEST_TARGET) but cannot run"; \
	fi
endif

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) $(WIN_TEST_TARGET) $(DETECT_OUT) *.o tests/*.o

lint:
	cppcheck --enable=warning,style,performance,portability --std=c11 src/
