# lib/lib.mk - builds the static library build/libcgra.a
# (serial transport, accelerated kernels, and the in-process sim: emulator).

CC     ?= cc
AR     ?= ar
GROFF  ?= groff
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Iinclude

BUILD = build
OBJS  = $(BUILD)/cgra.o $(BUILD)/emu.o $(BUILD)/serve.o
MAN3  = doc/libcgra.3

.PHONY: all man clean

all: $(BUILD)/libcgra.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/cgra.h src/emu.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libcgra.a: $(OBJS)
	$(AR) rcs $@ $^

# API docs live in the header comments and the hand-written man page. No
# doxygen: lint the page and render a text copy for a quick read.
man: | $(BUILD)
	$(GROFF) -man -z $(MAN3)                 # lint (warnings to stderr)
	$(GROFF) -man -Tutf8 $(MAN3) > $(BUILD)/libcgra.3.txt

clean:
	rm -rf $(BUILD)
