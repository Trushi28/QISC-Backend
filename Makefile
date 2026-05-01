CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -g
CPPFLAGS ?= -Iinclude/qisc
LDFLAGS ?= -pthread

BUILD_DIR := build
LIB := $(BUILD_DIR)/libqisc_backend.a

SRCS := \
	src/core/qisc_ir.c \
	src/analysis/qisc_cfg.c \
	src/analysis/qisc_ssa.c \
	src/analysis/qisc_opt.c \
	src/backend/qisc_codegen.c \
	src/backend/qisc_backend.c \
	src/backend/qisc_backend_bridge.c \
	src/runtime/qisc_convergence.c \
	src/runtime/qisc_living_component.c \
	src/runtime/qisc_sandbox.c

OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean check sample test_conditional

all: $(LIB)

$(LIB): $(OBJS)
	@mkdir -p $(@D)
	@ar rcs $@ $^

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/qisc_sample: examples/qisc_sample.c $(LIB)
	@mkdir -p $(@D)
	@$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

sample: $(BUILD_DIR)/qisc_sample
	./$(BUILD_DIR)/qisc_sample

check: sample

test_conditional: sample
	@$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_conditional.c $(BUILD_DIR)/output.o -o $(BUILD_DIR)/test_conditional
	./$(BUILD_DIR)/test_conditional

clean:
	@rm -rf $(BUILD_DIR)
