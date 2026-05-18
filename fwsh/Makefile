# ══════════════════════════════════════════════════════
#  fwsh — Firmware Mini Shell  ‧  Makefile
# ══════════════════════════════════════════════════════
#
# 使用方式：
#   make          ← 編譯（預設目標）
#   make clean    ← 清除所有中間檔案和執行檔
#   make install  ← 安裝到 /usr/local/bin（需要 sudo）
#   make run      ← 編譯後直接執行
#   make debug    ← 開啟 AddressSanitizer 的除錯版本
#   make valgrind ← 用 Valgrind 偵測記憶體洩漏
#   make info     ← 印出所有已找到的原始碼檔案
#
# 目錄結構：
#   src/     ← C 原始碼
#   include/ ← 標頭檔
#   obj/     ← 中間目的碼（.o），由 make 自動建立
#   fwsh     ← 最終執行檔

# ── 工具鏈 ───────────────────────────────────────────
CC      := gcc

# ── 編譯旗標 ─────────────────────────────────────────
#   -Wall -Wextra -Wpedantic  啟用所有有用的警告
#   -std=c11                  使用 C11 標準（含 anonymous struct 等特性）
#   -g                        加入 DWARF 除錯資訊（gdb / valgrind 需要）
#   -O2                       最佳化等級 2（適合 release）
#   -Iinclude                 讓 #include "shell.h" 找得到 include/ 目錄
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 -g -O2 -Iinclude

# ── 連結旗標 ─────────────────────────────────────────
#   -lreadline  連結 GNU Readline 函式庫（行編輯、Tab 補全）
LDFLAGS := -lreadline

# ── 路徑設定 ─────────────────────────────────────────
SRC_DIR := src
OBJ_DIR := obj
BIN     := fwsh

# ── 自動收集所有 .c 原始碼 ───────────────────────────
#   wildcard 展開所有符合的檔案路徑
#   patsubst 把 src/xxx.c 轉成 obj/xxx.o（建構對應關係）
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# ── 顏色（讓輸出更易讀）─────────────────────────────
GREEN  := \033[1;32m
CYAN   := \033[1;36m
YELLOW := \033[1;33m
RESET  := \033[0m

# ══════════════════════════════════════════════════════
#  規則（Rules）
# ══════════════════════════════════════════════════════

# 預設目標：all（make 不帶引數時執行）
.PHONY: all clean install run debug valgrind info

all: $(OBJ_DIR) $(BIN)
	@printf "$(GREEN)✓ Build successful: $(BIN)$(RESET)\n"

# 建立 obj/ 目錄（若不存在）
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# 最終連結：把所有 .o 合併成執行檔
#   $@  = 目標（fwsh）
#   $^  = 所有依賴（所有 .o 檔案）
$(BIN): $(OBJS)
	@printf "$(CYAN)Linking $@...$(RESET)\n"
	$(CC) $^ -o $@ $(LDFLAGS)

# 通用編譯規則：src/xxx.c → obj/xxx.o
#   $<  = 第一個依賴（.c 檔）
#   $@  = 目標（.o 檔）
#   -MMD -MP 自動產生 .d 依賴檔，讓 make 知道當標頭檔改變時要重新編譯
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@printf "$(CYAN)Compiling $<...$(RESET)\n"
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# 引入自動產生的依賴檔（若不存在則忽略，用 - 前置）
-include $(OBJS:.o=.d)

# 清除所有中間產物
clean:
	@printf "$(YELLOW)Cleaning...$(RESET)\n"
	@rm -rf $(OBJ_DIR) $(BIN)
	@printf "$(GREEN)✓ Clean done$(RESET)\n"

# 安裝到系統路徑
install: $(BIN)
	@printf "$(CYAN)Installing to /usr/local/bin/$(BIN)...$(RESET)\n"
	install -m 755 $(BIN) /usr/local/bin/
	@printf "$(GREEN)✓ Installed. Run 'fwsh' from anywhere.$(RESET)\n"

# 編譯後直接執行
run: all
	./$(BIN)

# 除錯版本：啟用 AddressSanitizer 偵測記憶體錯誤
debug: CFLAGS += -O0 -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: clean all
	@printf "$(YELLOW)⚠ Debug build with AddressSanitizer enabled$(RESET)\n"
	./$(BIN)

# Valgrind 記憶體洩漏分析
valgrind: all
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --verbose ./$(BIN)

# 印出專案資訊
info:
	@printf "$(GREEN)fwsh Project Info$(RESET)\n"
	@printf "  Sources : $(SRCS)\n"
	@printf "  Objects : $(OBJS)\n"
	@printf "  CFLAGS  : $(CFLAGS)\n"
	@printf "  LDFLAGS : $(LDFLAGS)\n"
