# c/Makefile -- build the M4.1a JSON layer test binary.
# Host cc, C11, no external libraries (M4 contract).
#   make            build ../build/arcforge_json_test
#   make test       build + run the vectors
#   make SAN=1      build with -fsanitize=undefined,address (acceptance #5)
#   make clean      remove the binaries
CC ?= cc
BUILD := ../build
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
BIN := $(BUILD)/arcforge_json_test

ifeq ($(SAN),1)
CFLAGS += -fsanitize=undefined,address -fno-omit-frame-pointer -g
BIN := $(BUILD)/arcforge_json_test_san
endif

all: $(BIN)

$(BIN): json.c json_test.c json.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ json.c json_test.c

$(BUILD):
	mkdir -p $(BUILD)

test: $(BIN)
	$(BIN)

clean:
	rm -f $(BUILD)/arcforge_json_test $(BUILD)/arcforge_json_test_san

.PHONY: all test clean
