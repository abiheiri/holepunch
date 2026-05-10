# holepunch C build
# Supports: Linux (x86_64, aarch64), macOS (x86_64, arm64)

NAME    := punch
VERSION ?= $(shell git describe --tags --always 2>/dev/null || echo v0.0.0)
SRCDIR  := src
BUILDIR := build

SRCS    := $(SRCDIR)/main.c \
           $(SRCDIR)/upnp.c \
           $(SRCDIR)/update.c

OBJS    := $(patsubst $(SRCDIR)/%.c,$(BUILDIR)/%.o,$(SRCS))

ifdef APPEND_CFLAGS
CFLAGS += $(APPEND_CFLAGS)
endif

CFLAGS  += -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS  += -Wall -Wextra -Werror -O2
CFLAGS  += -DVERSION=\"$(VERSION)\"

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Allow overriding the target architecture for cross-compilation
ARCH ?= $(UNAME_M)

# Try pkg-config for miniupnpc, fall back to defaults
MINIUPNPC_CFLAGS := $(shell pkg-config --cflags miniupnpc 2>/dev/null)
MINIUPNPC_LIBS   := $(shell pkg-config --libs miniupnpc 2>/dev/null)

ifeq ($(MINIUPNPC_LIBS),)
    MINIUPNPC_LIBS := -lminiupnpc
endif

LDFLAGS += $(MINIUPNPC_LIBS)
CFLAGS  += $(MINIUPNPC_CFLAGS)

ifeq ($(UNAME_S),Linux)
    ifeq ($(CC),aarch64-linux-gnu-gcc)
        TARGET := aarch64-unknown-linux-gnu
    else
        TARGET := x86_64-unknown-linux-gnu
    endif
endif

ifeq ($(UNAME_S),Darwin)
    ifeq ($(ARCH),arm64)
        TARGET := aarch64-apple-darwin
    else
        TARGET := x86_64-apple-darwin
    endif
endif

.PHONY: all clean dist

BIN := $(BUILDIR)/$(NAME)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILDIR)/%.o: $(SRCDIR)/%.c | $(BUILDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDIR):
	mkdir -p $@

clean:
	rm -rf $(BUILDIR) dist

dist: $(BIN)
	mkdir -p dist
	cp $(BIN) dist/
	tar -czf dist/$(NAME)-$(TARGET).tar.gz -C dist $(NAME)
	rm dist/$(NAME)
