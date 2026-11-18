# c/Makefile -- build the M4 C binaries.
# Host cc, C11, no external libraries (M4 contract).
#   make                 build ../build/arcforge_json_test + ../build/arcforge
#   make test            build + run the JSON vectors
#   make SAN=1           build with -fsanitize=undefined,address (acceptance #5)
#   make clean           remove the binaries
CC ?= cc
BUILD := ../build
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
JSON_TEST := $(BUILD)/arcforge_json_test
ARCFORGE := $(BUILD)/arcforge

ifeq ($(SAN),1)
CFLAGS += -fsanitize=undefined,address -fno-omit-frame-pointer -g
JSON_TEST := $(BUILD)/arcforge_json_test_san
ARCFORGE := $(BUILD)/arcforge_san
endif

all: $(JSON_TEST) $(ARCFORGE)

$(JSON_TEST): json.c json_test.c json.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ json.c json_test.c

$(ARCFORGE): json.c schema.c main.c json.h schema.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ json.c schema.c main.c

$(BUILD):
	mkdir -p $(BUILD)

test: $(JSON_TEST)
	$(JSON_TEST)

clean:
	rm -f $(BUILD)/arcforge_json_test $(BUILD)/arcforge_json_test_san \
	      $(BUILD)/arcforge $(BUILD)/arcforge_san

.PHONY: all test clean
