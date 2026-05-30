/*
 * mq_demo.c - Message Queue 操作示範
 *
 * 流程：
 *   1. 開啟 /dev/mq_ipc。
 *   2. 寫入少量固定大小訊息。
 *   3. 讀回同樣數量的訊息並列印耗時。
 *   4. 顯示 /proc/mq_stats，確認 kernel 端統計。
 *
 * 執行前需先載入 mq_module.ko，可用 scripts/02_demo.sh 一起操作。
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "common.h"

#define DEMO_N  8

int main(void)
{
    int     fd;
    char    wbuf[MSG_SIZE], rbuf[MSG_SIZE];
    int     i;
    double  t0, t1;

    printf("┌──────────────────────────────────────────────────────────┐\n");
    printf("│          Message Queue Demo   (/dev/mq_ipc)              │\n");
    printf("├──────────────────────────────────────────────────────────┤\n");
    printf("│  write() → copy_from_user → kfifo  (user→kernel copy)   │\n");
    printf("│  read()  → kfifo → copy_to_user    (kernel→user copy)   │\n");
    printf("└──────────────────────────────────────────────────────────┘\n\n");

    fd = open(MQ_DEVICE, O_RDWR);
    if (fd < 0) { perror("open " MQ_DEVICE); return 1; }

    printf("[Producer]  Enqueue %d messages\n\n", DEMO_N);
    for (i = 0; i < DEMO_N; i++) {
        snprintf(wbuf, MSG_SIZE, "MQ-MSG[%02d] data=%08x", i, i * 0xBEEF);
        t0 = now_us();
        write(fd, wbuf, MSG_SIZE);
        t1 = now_us();
        printf("  enq[%02d]  %-36s  Δ=%.1f µs\n", i, wbuf, t1 - t0);
    }

    printf("\n[Consumer]  Dequeue %d messages\n\n", DEMO_N);
    for (i = 0; i < DEMO_N; i++) {
        t0 = now_us();
        read(fd, rbuf, MSG_SIZE);
        t1 = now_us();
        rbuf[MSG_SIZE-1] = '\0';
        printf("  deq[%02d]  %-36s  Δ=%.1f µs\n", i, rbuf, t1 - t0);
    }

    printf("\n[/proc/mq_stats]\n");
    system("cat /proc/mq_stats | sed 's/^/  /'");

    close(fd);
    return 0;
}
