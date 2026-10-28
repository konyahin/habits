# Native build (on iSH: apk add build-base):   make
# Cross build from macOS/Linux via zig:        make cross
# Cross build and copy to iCloud logs/bin:     make deploy

PROG    = habits
SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?=

# iSH emulates a plain i686: no SSE assumed, static musl binary.
CROSS_TARGET = x86-linux-musl
CROSS_CPU    = i686
CROSS_CC     = zig cc -target $(CROSS_TARGET) -mcpu=$(CROSS_CPU)

DEST ?= $(HOME)/Library/Mobile Documents/com~apple~CloudDocs/logs/bin

all: $(PROG)

$(PROG): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

cross:
	$(MAKE) clean
	$(MAKE) CC="$(CROSS_CC)" LDFLAGS="-static -s"

deploy: cross
	cp $(PROG) "$(DEST)/"

clean:
	rm -f $(PROG) $(OBJ)

.PHONY: all cross deploy clean
