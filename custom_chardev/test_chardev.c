/* test_chardev.c
 * 使用者空間完整測試: open/write/read/ioctl
 * gcc -Wall -o test_chardev test_chardev.c
 * sudo ./test_chardev
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "chardev.h"

#define DEVICE_PATH  "/dev/chardev0"

int main(void) {
    int  fd;
    char wbuf[] = "Hello from userspace!";
    char rbuf[256];
    chardev_stats_t stats;
    unsigned int new_size = 8192;

    /* 1. open */
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    /* 2. write */
    write(fd, wbuf, strlen(wbuf));

    /* 3. read (rewind first) */
    lseek(fd, 0, SEEK_SET);
    read(fd, rbuf, sizeof(rbuf) - 1);
    printf("read: %s
", rbuf);

    /* 4. ioctl GETSTATS */
    ioctl(fd, CHARDEV_IOCTL_GETSTATS, &stats);
    printf("writes=%lu reads=%lu used=%u
",
           stats.write_count, stats.read_count,
           stats.buffer_used);

    /* 5. ioctl SETSIZE → 8 KB */
    ioctl(fd, CHARDEV_IOCTL_SETSIZE, &new_size);

    /* 6. ioctl CLEAR */
    ioctl(fd, CHARDEV_IOCTL_CLEAR, 0);

    /* 7. 驗證 CLEAR 後為 EOF */
    lseek(fd, 0, SEEK_SET);
    ssize_t n = read(fd, rbuf, sizeof(rbuf));
    printf("after CLEAR read() = %zd (expect 0)
", n);

    close(fd);
    return 0;
}
