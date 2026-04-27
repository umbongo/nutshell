# Makefile for Nutshell

CC = x86_64-w64-mingw32-gcc
WINDRES = x86_64-w64-mingw32-windres
VCPKG_LIB = $(HOME)/vcpkg/installed/x64-mingw-gcc-static/lib
VCPKG_INC = $(HOME)/vcpkg/installed/x64-mingw-gcc-static/include
CFLAGS = -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion \
         -Os -ffunction-sections -fdata-sections -flto \
         -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$(VCPKG_INC) -Isrc/term -Isrc/ssh -Isrc/ui
LDFLAGS = -mwindows -Os -flto -Wl,--gc-sections -s \
          -L$(VCPKG_LIB) -lssh2 -lssl -lcrypto -lzlib -lcrypt32 -lbcrypt \
          -lgdiplus -lole32 -lshlwapi -lws2_32 -lgdi32 -luser32 -lcomctl32 -ldwmapi -lwinhttp -lm

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

# Test configuration (Native Linux)
TEST_CC = gcc
TEST_CFLAGS = -std=c11 -Wall -Wextra -g \
              -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui \
              -D_TEST -Wno-unused-function

# Auto-detect libssh2 availability for test linking
HAS_LIBSSH2 := $(shell echo 'int main(){}' | gcc -xc - -lssh2 -lssl -lcrypto -o /dev/null 2>/dev/null && echo yes || echo no)
# Auto-detect OpenSSL availability (may be present even without libssh2)
HAS_OPENSSL := $(shell echo 'int main(){}' | gcc -xc - -lssl -lcrypto -o /dev/null 2>/dev/null && echo yes || echo no)

ifeq ($(HAS_LIBSSH2),yes)
TEST_LDFLAGS = -lssh2 -lssl -lcrypto -lm
else ifeq ($(HAS_OPENSSL),yes)
TEST_CFLAGS += -DNO_SSH_LIBS
TEST_LDFLAGS = -lssl -lcrypto -lm
else
TEST_CFLAGS += -DNO_SSH_LIBS
TEST_LDFLAGS = -lm
endif

# Exclude Windows-only UI files and main entry point from test build
NON_TEST_SRCS = src/main.c $(wildcard src/ui/*.c)

# Exclude SSH/knownhosts networking files when libssh2 is unavailable
ifeq ($(HAS_LIBSSH2),no)
NON_TEST_SRCS += src/term/ssh_session.c src/term/ssh_channel.c src/term/ssh_pty.c \
                 src/config/ssh_io.c src/term/knownhosts.c
NON_TEST_IMPL = tests/test_ssh.c tests/session.c tests/test_knownhosts.c tests/test_key_auth.c
else
NON_TEST_IMPL =
endif

# All test implementation files
TEST_IMPL_SRCS = $(filter-out $(NON_TEST_IMPL),$(wildcard tests/*.c))

# All source files, excluding non-test sources
APP_SRCS = $(filter-out $(NON_TEST_SRCS),$(SRCS))

TEST_SRCS = $(APP_SRCS) $(TEST_IMPL_SRCS)
TEST_TARGET = build/test_runner


.PHONY: all clean test lint debug release redraw-debug wintest

# ── Win32-only test harness (icon renderer). Links GDI+ via MinGW,
# runs the resulting .exe under Wine if available; otherwise prints
# a skip notice.  Kept separate from `test` which is native Linux.
WIN_TEST_CC      = x86_64-w64-mingw32-gcc
WIN_TEST_TARGET  = build/win/test_runner.exe
WIN_TEST_SRCS    = src/ui/icons.c tests/test_icons.c tests/win_runner.c
WIN_TEST_CFLAGS  = -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
                   -Wformat=2 -Wconversion \
                   -Isrc -Isrc/core -Isrc/config -Isrc/crypto \
                   -I$(VCPKG_INC) -Isrc/term -Isrc/ssh -Isrc/ui -Itests
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
	@if command -v wine >/dev/null 2>&1; then \
		wine $(WIN_TEST_TARGET); \
	else \
		echo "[skip] wine not installed — built $(WIN_TEST_TARGET) but cannot run"; \
	fi

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET) *.o tests/*.o

lint:
	cppcheck --enable=warning,style,performance,portability --std=c11 src/