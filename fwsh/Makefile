# ══════════════════════════════════════════════════════
#  fwsh - Firmware Mini Shell Makefile
# ══════════════════════════════════════════════════════
#
# 常用目標：
#   make          編譯 fwsh（預設目標）
#   make clean    清除 obj/ 與 fwsh 執行檔
#   make install  安裝到 /usr/local/bin（通常需要 sudo）
#   make run      編譯後直接執行
#   make debug    使用 AddressSanitizer/UBSan 建置並執行
#   make valgrind 使用 Valgrind 檢查記憶體問題
#   make info     顯示 Makefile 偵測到的檔案與旗標
#
# 目錄角色：
#   src/      C 原始碼
#   include/  標頭檔
#   obj/      中間目的碼，由 make 自動建立
#   fwsh      最終執行檔

# ── 工具鏈 ───────────────────────────────────────────
CC      := gcc

# ── 編譯旗標 ─────────────────────────────────────────
#   -Wall -Wextra -Wpedantic  開啟常見且有用的編譯警告
#   -std=c11                  使用 C11 語言標準
#   -g                        產生除錯資訊，方便 gdb/valgrind 使用
#   -O2                       啟用一般最佳化，保留合理除錯性
#   -Iinclude                 讓 #include "shell.h" 找到 include/ 目錄
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11 -g -O2 -Iinclude

# ── 連結旗標 ─────────────────────────────────────────
#   -lreadline  連結 GNU Readline，提供行編輯、歷史紀錄與 Tab 補全
LDFLAGS := -lreadline

# ── 路徑設定 ─────────────────────────────────────────
SRC_DIR := src
OBJ_DIR := obj
BIN     := fwsh

# ── 自動收集 C 原始碼 ────────────────────────────────
#   wildcard  找出 src/ 底下所有 .c。
#   patsubst  將 src/name.c 對應成 obj/name.o。
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# ── 終端機顏色，讓 make 輸出較容易閱讀 ───────────────
GREEN  := \033[1;32m
CYAN   := \033[1;36m
YELLOW := \033[1;33m
RESET  := \033[0m

# ══════════════════════════════════════════════════════
#  建置規則
# ══════════════════════════════════════════════════════

# 預設目標：直接輸入 make 時會執行 all。
.PHONY: all clean install run debug valgrind info

all: $(OBJ_DIR) $(BIN)
	@printf "$(GREEN)✓ Build successful: $(BIN)$(RESET)\n"

# 建立 obj/ 目錄；所有 .o 和 .d 都放這裡。
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

# 最終連結：
#   $@  目標檔，這裡是 fwsh。
#   $^  所有依賴檔，這裡是 obj/*.o。
$(BIN): $(OBJS)
	@printf "$(CYAN)Linking $@...$(RESET)\n"
	$(CC) $^ -o $@ $(LDFLAGS)

# 通用編譯規則：src/name.c -> obj/name.o
#   $<       第一個依賴檔，也就是目前要編譯的 .c。
#   $@       目標檔，也就是要輸出的 .o。
#   -MMD -MP 產生 .d 依賴檔，標頭檔變更時可自動重編。
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@printf "$(CYAN)Compiling $<...$(RESET)\n"
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# 引入 .d 依賴檔；前面的 - 表示檔案尚未產生時不要報錯。
-include $(OBJS:.o=.d)

# 清除建置產物，回到未編譯狀態。
clean:
	@printf "$(YELLOW)Cleaning...$(RESET)\n"
	@rm -rf $(OBJ_DIR) $(BIN)
	@printf "$(GREEN)✓ Clean done$(RESET)\n"

# 將 fwsh 安裝到系統路徑，讓使用者可直接輸入 fwsh 執行。
install: $(BIN)
	@printf "$(CYAN)Installing to /usr/local/bin/$(BIN)...$(RESET)\n"
	install -m 755 $(BIN) /usr/local/bin/
	@printf "$(GREEN)✓ Installed. Run 'fwsh' from anywhere.$(RESET)\n"

# 編譯成功後直接啟動 fwsh。
run: all
	./$(BIN)

# 除錯建置：啟用 AddressSanitizer 與 UndefinedBehaviorSanitizer。
debug: CFLAGS += -O0 -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
debug: LDFLAGS += -fsanitize=address -fsanitize=undefined
debug: clean all
	@printf "$(YELLOW)⚠ Debug build with AddressSanitizer enabled$(RESET)\n"
	./$(BIN)

# 使用 Valgrind 檢查 heap 使用、洩漏與未初始化讀取。
valgrind: all
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --verbose ./$(BIN)

# 顯示 Makefile 目前偵測到的來源檔、目的檔與旗標。
info:
	@printf "$(GREEN)fwsh Project Info$(RESET)\n"
	@printf "  Sources : $(SRCS)\n"
	@printf "  Objects : $(OBJS)\n"
	@printf "  CFLAGS  : $(CFLAGS)\n"
	@printf "  LDFLAGS : $(LDFLAGS)\n"
