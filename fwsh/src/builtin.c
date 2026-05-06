/*
 * builtin.c — 內建指令完整實作
 *
 * 一般 Shell 工具：
 *   cd, pwd, exit/quit, help, history, clear
 *
 * 韌體工程師專屬工具（這些是履歷亮點）：
 *   hexdump  - Hex + ASCII 雙欄顯示檔案內容（韌體映像除錯必備）
 *   crc32    - CRC-32 檢查碼計算（韌體完整性驗證）
 *   memmap   - 顯示 /proc/iomem 實體記憶體佈局（了解目標板記憶體配置）
 *
 * 分派機制：
 *   使用「函式指標表」（dispatch table）設計。
 *   新增指令只需：① 實作函式 ② 在 builtins[] 陣列加一筆記錄。
 *   不需要修改 is_builtin() 或 exec_builtin() 的邏輯。
 *   這種設計在韌體的「命令表」（command table）驅動架構中也很常見。
 */

#include "builtin.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shell.h"

/* ── 前置宣告 ─────────────────────────────────────── */
static int builtin_cd(Cmd* cmd);
static int builtin_pwd(Cmd* cmd);
static int builtin_exit(Cmd* cmd);
static int builtin_help(Cmd* cmd);
static int builtin_history(Cmd* cmd);
static int builtin_clear(Cmd* cmd);
static int builtin_hexdump(Cmd* cmd);
static int builtin_crc32(Cmd* cmd);
static int builtin_memmap(Cmd* cmd);

/* ── 分派表 ───────────────────────────────────────── */
/*
 * 函式指標表：以結構陣列儲存「名稱 → 函式 → 說明」的對應關係。
 * 最後一筆 name == NULL 作為哨兵（sentinel），用於終止迴圈搜尋。
 */
typedef struct {
  const char* name;
  int (*func)(Cmd*);
  const char* desc;
} BuiltinEntry;

static BuiltinEntry builtins[] = {
    /* 一般指令 */
    {"cd", builtin_cd,
     "cd <dir>              Change directory (支援 ~ 和 -)          "},
    {"pwd", builtin_pwd,
     "pwd                   Print working directory                  "},
    {"exit", builtin_exit,
     "exit [code]           Exit shell with optional status code     "},
    {"quit", builtin_exit,
     "quit                  Alias for exit                           "},
    {"help", builtin_help,
     "help                  Show this help message                   "},
    {"history", builtin_history,
     "history               Show command history                     "},
    {"clear", builtin_clear,
     "clear                 Clear terminal screen                    "},
    /* 韌體工程師工具 */
    {"hexdump", builtin_hexdump,
     "hexdump <file> [len]  Hex+ASCII dump (supports 0x prefix)     "},
    {"crc32", builtin_crc32,
     "crc32 <file>          Compute CRC-32 checksum (IEEE 802.3)     "},
    {"memmap", builtin_memmap,
     "memmap                Show /proc/iomem physical memory layout  "},
    /* 哨兵 */
    {NULL, NULL, NULL}};

/* ── 公開介面 ─────────────────────────────────────── */

int is_builtin(const char* name) {
  for (int i = 0; builtins[i].name; i++)
    if (strcmp(name, builtins[i].name) == 0) return 1;
  return 0;
}

int exec_builtin(Cmd* cmd) {
  for (int i = 0; builtins[i].name; i++)
    if (strcmp(cmd->argv[0], builtins[i].name) == 0)
      return builtins[i].func(cmd);
  return -1; /* 不應執行到此 */
}

/* ═══════════════════════════════════════════════════
 * 一般 Shell 指令實作
 * ═══════════════════════════════════════════════════ */

/*
 * cd - 切換工作目錄
 *
 * 支援特殊引數：
 *   cd         → 回到 HOME
 *   cd ~       → 回到 HOME
 *   cd -       → 切換到上一個目錄（OLDPWD），並印出新路徑
 *   cd <path>  → 切換到指定路徑
 *
 * OLDPWD 環境變數讓 cd - 成為可能。
 * 每次 cd 成功後都要更新 OLDPWD。
 */
static int builtin_cd(Cmd* cmd) {
  const char* dir;

  if (cmd->argc < 2 || strcmp(cmd->argv[1], "~") == 0) {
    dir = getenv("HOME");
    if (!dir) {
      fprintf(stderr, "cd: HOME environment variable not set\n");
      return 1;
    }
  } else if (strcmp(cmd->argv[1], "-") == 0) {
    dir = getenv("OLDPWD");
    if (!dir) {
      fprintf(stderr, "cd: OLDPWD not set (no previous directory)\n");
      return 1;
    }
    printf("%s\n", dir); /* cd - 習慣上會印出新路徑 */
  } else {
    dir = cmd->argv[1];
  }

  /* 先記錄目前目錄為 OLDPWD */
  char old[512];
  if (getcwd(old, sizeof(old))) setenv("OLDPWD", old, 1);

  /* 實際切換 */
  if (chdir(dir) != 0) {
    fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
    return 1;
  }
  return 0;
}

/* pwd - 印出工作目錄 */
static int builtin_pwd(Cmd* cmd) {
  (void)cmd; /* 不使用引數 */
  char cwd[512];
  if (getcwd(cwd, sizeof(cwd)))
    printf("%s\n", cwd);
  else
    perror("pwd");
  return 0;
}

/*
 * exit - 結束 Shell
 *
 * 設定 g_shell.running = 0 → shell_run() 的 while 迴圈結束 → main() 返回。
 * 可選引數為結束碼（預設 0）。
 */
static int builtin_exit(Cmd* cmd) {
  int code = 0;
  if (cmd->argc >= 2) code = atoi(cmd->argv[1]);

  g_shell.running = 0;
  printf(COLOR_GREEN "Goodbye! fwsh exiting with code %d.\n" COLOR_RESET, code);
  return code;
}

/* help - 顯示所有指令說明 */
static int builtin_help(Cmd* cmd) {
  (void)cmd;
  printf(COLOR_GREEN "fwsh v%s — Firmware Mini Shell\n" COLOR_RESET,
         FWSH_VERSION);
  printf("─────────────────────────────────────────────────────────────────\n");
  printf("  " COLOR_CYAN "General Commands\n" COLOR_RESET);
  for (int i = 0; builtins[i].name; i++) {
    if (strcmp(builtins[i].name, "hexdump") == 0)
      printf("\n  " COLOR_CYAN "Firmware Engineer Tools\n" COLOR_RESET);
    printf("  %s\n", builtins[i].desc);
  }
  printf("─────────────────────────────────────────────────────────────────\n");
  printf("  Supports: pipes (|), redirection (< > >>), background (&)\n");
  printf("  Tip: Use Ctrl+R to reverse-search history, Tab to autocomplete.\n");
  return 0;
}

/* history - 顯示指令歷史 */
static int builtin_history(Cmd* cmd) {
  (void)cmd;
  if (g_shell.hist_count == 0) {
    printf("  (no history)\n");
    return 0;
  }
  int start = g_shell.hist_head - g_shell.hist_count;
  for (int i = 0; i < g_shell.hist_count; i++) {
    int idx = (start + i) % MAX_HISTORY;
    printf("  %3d  %s\n", i + 1, g_shell.history[idx]);
  }
  return 0;
}

/* clear - 清除終端機畫面 */
static int builtin_clear(Cmd* cmd) {
  (void)cmd;
  /*
   * ANSI escape codes：
   *   \033[2J  = 清除整個畫面
   *   \033[H   = 游標移到左上角 (row 1, col 1)
   */
  printf("\033[2J\033[H");
  fflush(stdout);
  return 0;
}

/* ═══════════════════════════════════════════════════
 * 韌體工程師專屬工具實作
 * ═══════════════════════════════════════════════════ */

/*
 * builtin_hexdump - Hex + ASCII 雙欄顯示
 *
 * 用途：在不借助外部工具的情況下，快速檢視韌體映像檔、
 *       Flash 轉儲、暫存器 shadow 緩衝區等二進位資料。
 *
 * 顯示格式（每列 16 bytes）：
 *   Offset  | 左半部 8 bytes | 右半部 8 bytes | ASCII 表示
 *
 * 與 `hexdump -C` 的差異：
 *   - 這是「不需要安裝任何套件」的自包含版本
 *   - 支援以 0x 前置的十六進位 len 引數
 *   - 已整合彩色輸出
 *
 * 使用範例：
 *   hexdump /boot/vmlinuz 0x80   ← 顯示前 128 bytes
 *   hexdump /dev/mem 256         ← 顯示前 256 bytes
 */
static int builtin_hexdump(Cmd* cmd) {
  if (cmd->argc < 2) {
    fprintf(stderr, "Usage: hexdump <file> [max_bytes]\n");
    fprintf(stderr, "       max_bytes supports 0x prefix (e.g. 0x100 = 256)\n");
    return 1;
  }

  /* strtol 的 base=0 讓它自動識別 0x（十六進位）和 0（八進位）前置 */
  long max_bytes = 256;
  if (cmd->argc >= 3) {
    char* endptr;
    max_bytes = strtol(cmd->argv[2], &endptr, 0);
    if (max_bytes <= 0 || *endptr != '\0') {
      fprintf(stderr, "hexdump: invalid length '%s'\n", cmd->argv[2]);
      return 1;
    }
  }

  FILE* fp =
      fopen(cmd->argv[1], "rb"); /* "rb" = binary mode，避免 Windows 換行轉換 */
  if (!fp) {
    fprintf(stderr, "hexdump: %s: %s\n", cmd->argv[1], strerror(errno));
    return 1;
  }

  printf(COLOR_GREEN "Hexdump: %s  (max %ld / 0x%lX bytes)\n" COLOR_RESET,
         cmd->argv[1], max_bytes, max_bytes);
  printf(
      "Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F"
      "  |ASCII           |\n");
  printf(
      "────────  ───────────────────────  ───────────────────────"
      "  ─────────────────\n");

  uint8_t buf[16];
  long offset = 0;
  size_t n;

  while (offset < max_bytes) {
    /* 計算本列最多能讀幾 bytes */
    size_t to_read =
        (size_t)((max_bytes - offset) < 16 ? (max_bytes - offset) : 16);
    n = fread(buf, 1, to_read, fp);
    if (n == 0) break;

    /* 欄位 1：偏移量（8 位十六進位）*/
    printf("%08lX  ", offset);

    /* 欄位 2：十六進位 bytes，分兩組各 8 bytes，中間加空格 */
    for (int i = 0; i < 16; i++) {
      if (i == 8) printf(" "); /* 中間分隔空格，提升可讀性 */
      if (i < (int)n)
        printf("%02X ", buf[i]);
      else
        printf("   "); /* 最後一列不足 16 bytes 時填充空格 */
    }

    /* 欄位 3：ASCII 表示（不可見字元以 '.' 代替） */
    printf(" |");
    for (size_t i = 0; i < n; i++)
      printf("%c", (buf[i] >= 0x20 && buf[i] < 0x7F) ? (char)buf[i] : '.');
    for (size_t i = n; i < 16; i++) printf(" ");
    printf("|\n");

    offset += (long)n;
  }

  fclose(fp);
  printf("────────\n");
  printf("Total: %ld bytes (0x%lX) displayed.\n", offset, offset);
  return 0;
}

/* ── CRC-32 查找表（IEEE 802.3 多項式 0xEDB88320）──── */

/*
 * CRC-32 在韌體開發中的重要性：
 *   - Bootloader 在跳轉到 Application 前，用 CRC-32 驗證 Flash 內容
 *   - OTA 更新時驗證下載的韌體映像是否損壞
 *   - 通訊協定（UART、SPI frame）的資料完整性校驗
 *
 * 演算法原理：
 *   使用「查找表法」（table-driven CRC）大幅提升速度。
 *   預先計算每個 byte 值（0~255）的 CRC 貢獻，存入 256 筆的查找表。
 *   實際計算時，對每個 byte 只需一次 XOR + 一次查表 + 一次右移。
 */
static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

static void crc32_build_table(void) {
  /*
   * IEEE 802.3 反射多項式（bit-reversed polynomial）：
   *   原始多項式 0x04C11DB7 的反射形式為 0xEDB88320。
   *   使用反射形式是因為它從 LSB 開始處理，實作更簡單。
   */
  for (uint32_t byte = 0; byte < 256; byte++) {
    uint32_t crc = byte;
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 1)
        crc = 0xEDB88320U ^ (crc >> 1);
      else
        crc >>= 1;
    }
    crc32_table[byte] = crc;
  }
  crc32_table_ready = 1;
}

static int builtin_crc32(Cmd* cmd) {
  if (cmd->argc < 2) {
    fprintf(stderr, "Usage: crc32 <file>\n");
    return 1;
  }

  FILE* fp = fopen(cmd->argv[1], "rb");
  if (!fp) {
    fprintf(stderr, "crc32: %s: %s\n", cmd->argv[1], strerror(errno));
    return 1;
  }

  if (!crc32_table_ready) crc32_build_table();

  /*
   * CRC-32 計算步驟：
   *   1. 初始值設為 0xFFFFFFFF（全 1 的初始化向量）
   *   2. 對每個輸入 byte 更新 CRC：
   *      crc = table[(crc XOR byte) & 0xFF] XOR (crc >> 8)
   *   3. 最終結果與 0xFFFFFFFF 做 XOR（位元反轉完成）
   *
   * 為何初始化為 0xFFFFFFFF 又最終 XOR？
   *   確保全 0 的輸入不會產生全 0 的 CRC（避免偽陰性）。
   */
  uint32_t crc = 0xFFFFFFFFU;
  uint8_t chunk[4096];
  size_t n;
  long total = 0;

  while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
    for (size_t i = 0; i < n; i++)
      crc = crc32_table[(crc ^ chunk[i]) & 0xFF] ^ (crc >> 8);
    total += (long)n;
  }
  fclose(fp);

  crc ^= 0xFFFFFFFFU; /* 最終位元反轉 */

  printf(COLOR_YELLOW "CRC32" COLOR_RESET "(%s) = " COLOR_GREEN
                      "0x%08X" COLOR_RESET "  (%ld bytes, %ld KB)\n",
         cmd->argv[1], crc, total, total / 1024);
  return 0;
}

/*
 * memmap - 顯示 /proc/iomem 實體記憶體佈局
 *
 * /proc/iomem 記錄了系統的實體位址空間配置，
 * 對韌體工程師和 BSP（Board Support Package）工程師非常重要：
 *   - 確認 System RAM 起始位址和大小
 *   - 查看 Kernel 和 initrd 被載入到哪個實體位址
 *   - 確認 PCI / ACPI 設備的 MMIO 位址範圍
 *   - 在 Device Tree 或 Linker Script 設定時做交叉核對
 *
 * 彩色標示：
 *   黃色 = System RAM（最重要，可用記憶體）
 *   青色 = Kernel 相關區域
 *   洋紅 = ACPI / PCI 設備
 *   白色 = 其他
 */
static int builtin_memmap(Cmd* cmd) {
  (void)cmd;

  FILE* fp = fopen("/proc/iomem", "r");
  if (!fp) {
    fprintf(stderr, "memmap: cannot open /proc/iomem: %s\n", strerror(errno));
    fprintf(stderr,
            "       (This file requires Linux kernel; "
            "may not exist in containers)\n");
    return 1;
  }

  printf(COLOR_GREEN "Physical Memory Map (/proc/iomem)\n" COLOR_RESET);
  printf("─────────────────────────────────────────────────────\n");
  printf(COLOR_YELLOW "  Yellow" COLOR_RESET " = System RAM  " COLOR_CYAN
                      "  Cyan" COLOR_RESET " = Kernel  " COLOR_MAGENTA
                      " Magenta" COLOR_RESET " = ACPI/PCI\n");
  printf("─────────────────────────────────────────────────────\n");

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    /* 根據關鍵字選擇顏色 */
    if (strstr(line, "System RAM"))
      printf(COLOR_YELLOW "%s" COLOR_RESET, line);
    else if (strstr(line, "Kernel") || strstr(line, "kernel") ||
             strstr(line, "initrd"))
      printf(COLOR_CYAN "%s" COLOR_RESET, line);
    else if (strstr(line, "ACPI") || strstr(line, "PCI") ||
             strstr(line, "Reserved"))
      printf(COLOR_MAGENTA "%s" COLOR_RESET, line);
    else
      printf("%s", line);
  }

  fclose(fp);
  return 0;
}
