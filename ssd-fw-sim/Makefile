PROJECT := ssd_fw_sim
TEST_PROJECT := ssd_fw_sim_tests

# 編譯設定：開啟嚴格警告，讓型別與未使用變數等問題提早暴露。
CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude
LDFLAGS :=

# 目錄設定集中在這裡，方便日後調整專案結構。
SRC_DIR := src
OBJ_DIR := build
TEST_DIR := tests

# 主程式來源檔；main.c 只會被主程式使用。
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
# 測試程式連結 core 模組，但排除 main.c，避免重複入口點。
CORE_SRCS := $(filter-out $(SRC_DIR)/main.c,$(SRCS))
TEST_SRCS := $(TEST_DIR)/test_suite.c
TEST_OBJS := $(CORE_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o) $(TEST_SRCS:$(TEST_DIR)/%.c=$(OBJ_DIR)/tests/%.o)

all: $(PROJECT)

test: $(TEST_PROJECT)
	./$(TEST_PROJECT)

# 主程式與測試程式都使用同一組 CFLAGS，確保警告規則一致。
$(PROJECT): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(TEST_PROJECT): $(TEST_OBJS)
	$(CC) $(CFLAGS) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/tests/%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)/tests
	$(CC) $(CFLAGS) -c $< -o $@

# 建置目錄用 order-only prerequisite 建立，避免目錄時間戳造成不必要重編。
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/tests:
	mkdir -p $(OBJ_DIR)/tests

clean:
	rm -rf $(OBJ_DIR) $(PROJECT) $(TEST_PROJECT)

.PHONY: all clean test
