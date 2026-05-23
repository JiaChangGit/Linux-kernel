PROJECT := ssd_fw_sim
TEST_PROJECT := ssd_fw_sim_tests

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude
LDFLAGS :=

SRC_DIR := src
OBJ_DIR := build
TEST_DIR := tests

SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/config.c \
	$(SRC_DIR)/stats.c \
	$(SRC_DIR)/request.c \
	$(SRC_DIR)/nvme.c \
	$(SRC_DIR)/block_manager.c \
	$(SRC_DIR)/nand.c \
	$(SRC_DIR)/mapping.c \
	$(SRC_DIR)/gc.c \
	$(SRC_DIR)/ftl.c \
	$(SRC_DIR)/scheduler.c

OBJS := $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
CORE_SRCS := $(filter-out $(SRC_DIR)/main.c,$(SRCS))
TEST_SRCS := $(TEST_DIR)/test_suite.c
TEST_OBJS := $(CORE_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o) $(TEST_SRCS:$(TEST_DIR)/%.c=$(OBJ_DIR)/tests/%.o)

all: $(PROJECT)

test: $(TEST_PROJECT)
	./$(TEST_PROJECT)

$(PROJECT): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(TEST_PROJECT): $(TEST_OBJS)
	$(CC) $(CFLAGS) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/tests/%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)/tests
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/tests:
	mkdir -p $(OBJ_DIR)/tests

clean:
	rm -rf $(OBJ_DIR) $(PROJECT) $(TEST_PROJECT)

.PHONY: all clean test
