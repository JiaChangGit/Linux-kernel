/**
 * test_app.c - chardev userspace 測試程式
 *
 * 測試流程：
 *  1. open() 開啟 /dev/chardev0。
 *  2. write() 寫入測試字串。
 *  3. lseek() 將 file position 移回開頭。
 *  4. read() 讀回剛才寫入的資料。
 *  5. ioctl() 測試 get length、set read-only、reset buffer。
 *
 * 建置：make -C userspace
 * 執行：sudo ./userspace/test_app
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../driver/chardev.h"

#define DEVICE "/dev/chardev0"
#define TEST_MSG "Hello from userspace!"

static void print_separator(const char* title) {
  printf("\n========== %s ==========\n", title);
}

int main(void) {
  int fd = -1;
  int len = 0;
  int rdonly = 0;
  int exit_code = EXIT_FAILURE;
  ssize_t nread;
  ssize_t nwritten;
  char rbuf[256] = {0};

  /* 1. Open：取得 file descriptor，後續 read/write/ioctl 都使用它。 */
  print_separator("OPEN");
  fd = open(DEVICE, O_RDWR);
  if (fd < 0) {
    perror("open");
    return EXIT_FAILURE;
  }
  printf("Opened %s successfully (fd=%d)\n", DEVICE, fd);

  /* 2. Write：把 userspace 字串複製到 driver 的 kernel buffer。 */
  print_separator("WRITE");
  nwritten = write(fd, TEST_MSG, strlen(TEST_MSG));
  if (nwritten < 0) {
    perror("write");
    goto out_close;
  }
  printf("write() returned %zd, wrote: \"%s\"\n", nwritten, TEST_MSG);

  /* 3. Read：同一個 fd 寫完後位置在尾端，因此先 lseek 回開頭。 */
  print_separator("READ");
  if (lseek(fd, 0, SEEK_SET) < 0) {
    perror("lseek");
    goto out_close;
  }

  nread = read(fd, rbuf, sizeof(rbuf) - 1);
  if (nread < 0) {
    perror("read");
    goto out_close;
  }
  rbuf[nread] = '\0';
  printf("read() returned %zd, content: \"%s\"\n", nread, rbuf);

  /* 4. IOCTL_GET_LEN：由 driver 回傳目前有效資料長度。 */
  print_separator("IOCTL GET_LEN");
  if (ioctl(fd, IOCTL_GET_LEN, &len) < 0) {
    perror("ioctl GET_LEN");
    goto out_close;
  }
  printf("Buffer length = %d\n", len);

  /* 5. IOCTL_SET_RDONLY：開啟唯讀模式，確認 write() 會被拒絕。 */
  print_separator("IOCTL SET_RDONLY");
  rdonly = 1;
  if (ioctl(fd, IOCTL_SET_RDONLY, &rdonly) < 0) {
    perror("ioctl SET_RDONLY on");
    goto out_close;
  }
  printf("Set read-only mode ON\n");

  nwritten = write(fd, "blocked write", 13);
  if (nwritten < 0) {
    printf("Write correctly blocked: %s\n", strerror(errno));
  } else {
    printf("Unexpected write success, bytes=%zd\n", nwritten);
    goto out_close;
  }

  /* 關閉唯讀模式，避免後續測試或手動操作被 read_only 擋住。 */
  rdonly = 0;
  if (ioctl(fd, IOCTL_SET_RDONLY, &rdonly) < 0) {
    perror("ioctl SET_RDONLY off");
    goto out_close;
  }
  printf("Set read-only mode OFF\n");

  /* 6. IOCTL_RESET_BUF：清空 kernel buffer，下一次 read 應回 EOF。 */
  print_separator("IOCTL RESET_BUF");
  if (ioctl(fd, IOCTL_RESET_BUF) < 0) {
    perror("ioctl RESET_BUF");
    goto out_close;
  }
  printf("Buffer reset\n");

  if (lseek(fd, 0, SEEK_SET) < 0) {
    perror("lseek after reset");
    goto out_close;
  }

  memset(rbuf, 0, sizeof(rbuf));
  nread = read(fd, rbuf, sizeof(rbuf));
  if (nread < 0) {
    perror("read after reset");
    goto out_close;
  }
  printf("After reset, read() returned %zd (expected 0 = EOF)\n", nread);

  exit_code = EXIT_SUCCESS;

out_close:
  /* 7. Close：釋放 userspace fd，driver 端會進入 release callback。 */
  print_separator("CLOSE");
  if (fd >= 0) close(fd);
  printf("Device closed\n");

  printf("\n[INFO] You can also check:\n");
  printf("  cat /proc/chardev_info\n");
  printf("  cat /sys/class/chardev/chardev0/stats\n");
  printf("  cat /sys/class/chardev/chardev0/buf_len\n");

  return exit_code;
}
