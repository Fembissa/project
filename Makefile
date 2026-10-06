CC ?= gcc

CSTD ?= $(shell echo 'int main(void){return 0;}' | \
	$(CC) -std=c23 -x c -fsyntax-only - 2>/dev/null && echo c23 || echo c2x)

BASE_FLAGS := -std=$(CSTD) -Wall -Wextra -D_POSIX_C_SOURCE=200809L
CFLAGS  ?= -O2
LDFLAGS ?=

TARGET := mysh
SRCS    := $(wildcard src/*.c)
OBJS    := $(patsubst src/%.c,build/%.o,$(SRCS))

.PHONY: all debug clean

all: $(TARGET)

debug:
	$(MAKE) clean
	$(MAKE) $(TARGET) CFLAGS='-g -O0 -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined'

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

build/%.o: src/%.c | build
	$(CC) $(BASE_FLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build $(TARGET)

-include $(OBJS:.o=.d)