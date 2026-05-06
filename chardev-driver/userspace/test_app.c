/**
 * test_app.c - 完整測試 chardev 驅動的所有介面
 * 編譯：gcc -o test_app test_app.c
 * 使用：sudo ./test_app
 */

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
  int fd, ret, len;
  char rbuf[256] = {0};

  /* ---- 1. Open ---- */
  print_separator("OPEN");
  fd = open(DEVICE, O_RDWR);
  if (fd < 0) {
    perror("open");
    return EXIT_FAILURE;
  }
  printf("Opened %s successfully (fd=%d)\n", DEVICE, fd);

  /* ---- 2. Write ---- */
  print_separator("WRITE");
  ret = write(fd, TEST_MSG, strlen(TEST_MSG));
  printf("write() returned %d, wrote: \"%s\"\n", ret, TEST_MSG);

  /* ---- 3. Read (lseek back to 0 first) ---- */
  print_separator("READ");
  lseek(fd, 0, SEEK_SET);
  ret = read(fd, rbuf, sizeof(rbuf) - 1);
  rbuf[ret] = '\0';
  printf("read() returned %d, content: \"%s\"\n", ret, rbuf);

  /* ---- 4. IOCTL: GET_LEN ---- */
  print_separator("IOCTL GET_LEN");
  ret = ioctl(fd, IOCTL_GET_LEN, &len);
  if (ret < 0)
    perror("ioctl GET_LEN");
  else
    printf("Buffer length = %d\n", len);

  /* ---- 5. IOCTL: SET_RDONLY ---- */
  print_separator("IOCTL SET_RDONLY");
  int rdonly = 1;
  ret = ioctl(fd, IOCTL_SET_RDONLY, &rdonly);
  printf("Set read-only mode ON, ret=%d\n", ret);

  /* 嘗試寫入（應該被拒絕）*/
  ret = write(fd, "blocked write", 13);
  if (ret < 0)
    printf("Write correctly blocked: %m\n"); /* %m = strerror(errno) */

  /* 關掉唯讀 */
  rdonly = 0;
  ioctl(fd, IOCTL_SET_RDONLY, &rdonly);
  printf("Set read-only mode OFF\n");

  /* ---- 6. IOCTL: RESET_BUF ---- */
  print_separator("IOCTL RESET_BUF");
  ret = ioctl(fd, IOCTL_RESET_BUF);
  printf("Buffer reset, ret=%d\n", ret);

  /* 確認 buffer 已清空 */
  lseek(fd, 0, SEEK_SET);
  memset(rbuf, 0, sizeof(rbuf));
  ret = read(fd, rbuf, sizeof(rbuf));
  printf("After reset, read() returned %d (expected 0 = EOF)\n", ret);

  /* ---- 7. Close ---- */
  print_separator("CLOSE");
  close(fd);
  printf("Device closed\n");

  printf("\n[INFO] Now check:\n");
  printf("  cat /proc/chardev_info\n");
  printf("  cat /sys/class/chardev/chardev0/stats\n");
  printf("  cat /sys/class/chardev/chardev0/buf_len\n");

  return EXIT_SUCCESS;
}
