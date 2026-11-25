# c/Makefile -- build the M4 C binaries.
# Host cc, C11, no external libraries (M4 contract).
#   make                 build ../build/arcforge_json_test + ../build/arcforge
#   make test            build + run the JSON vectors
#   make harness         build ../build/arcforge_expr_harness (M4.1c parity)
#   make render-harness  build ../build/arcforge_render_harness (M4.2 parity)
#   make SAN=1           build with -fsanitize=undefined,address (acceptance #5)
#   make clean           remove the binaries
CC ?= cc
BUILD := ../build
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
JSON_TEST := $(BUILD)/arcforge_json_test
ARCFORGE := $(BUILD)/arcforge
EXPR_HARNESS := $(BUILD)/arcforge_expr_harness
RENDER_HARNESS := $(BUILD)/arcforge_render_harness
# everything except main.c: the library the harnesses link against
ARCFORGE_LIB := json.c schema.c expr.c sim.c px.c validate.c raster.c sha256.c
ARCFORGE_SRC := $(ARCFORGE_LIB) main.c
ARCFORGE_HDR := json.h schema.h expr.h sim.h px.h validate.h raster.h sha256.h

ifeq ($(SAN),1)
CFLAGS += -fsanitize=undefined,address -fno-omit-frame-pointer -g
JSON_TEST := $(BUILD)/arcforge_json_test_san
ARCFORGE := $(BUILD)/arcforge_san
EXPR_HARNESS := $(BUILD)/arcforge_expr_harness_san
RENDER_HARNESS := $(BUILD)/arcforge_render_harness_san
endif

all: $(JSON_TEST) $(ARCFORGE)

$(JSON_TEST): json.c json_test.c json.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ json.c json_test.c

$(ARCFORGE): $(ARCFORGE_SRC) $(ARCFORGE_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ARCFORGE_SRC) -lm

$(EXPR_HARNESS): expr.c json.c expr.h json.h ../tests/expr_harness.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -o $@ ../tests/expr_harness.c expr.c json.c -lm

$(RENDER_HARNESS): $(ARCFORGE_LIB) $(ARCFORGE_HDR) ../tests/render_harness.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -o $@ ../tests/render_harness.c $(ARCFORGE_LIB) -lm

harness: $(EXPR_HARNESS)

render-harness: $(RENDER_HARNESS)

$(BUILD):
	mkdir -p $(BUILD)

test: $(JSON_TEST)
	$(JSON_TEST)

clean:
	rm -f $(BUILD)/arcforge_json_test $(BUILD)/arcforge_json_test_san \
	      $(BUILD)/arcforge $(BUILD)/arcforge_san \
	      $(BUILD)/arcforge_expr_harness $(BUILD)/arcforge_expr_harness_san \
	      $(BUILD)/arcforge_render_harness $(BUILD)/arcforge_render_harness_san

.PHONY: all test clean harness render-harness
