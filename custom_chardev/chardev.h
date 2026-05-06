/* chardev.h - 共用標頭 (kernel + userspace)
 * 定義 ioctl 命令碼與統計資料結構
 */

#ifndef _CHARDEV_H_
#define _CHARDEV_H_

#include <linux/ioctl.h>

/* ── ioctl Magic Number ── */
#define CHARDEV_MAGIC   'k'

/* ── GETSTATS 回傳結構 ── */
typedef struct {
    unsigned long read_count;   // 累計讀取次數
    unsigned long write_count;  // 累計寫入次數
    unsigned int  buffer_used;  // 已用 bytes
    unsigned int  buffer_size;  // 緩衝區總大小
} chardev_stats_t;

/* ── ioctl 命令定義 ─────────────────────────
 *  _IO   無資料傳輸
 *  _IOR  driver → user   (Read  from driver)
 *  _IOW  user  → driver  (Write to  driver)
 * ──────────────────────────────────────── */
#define CHARDEV_IOCTL_CLEAR    _IO (CHARDEV_MAGIC, 0)
#define CHARDEV_IOCTL_GETSTATS _IOR(CHARDEV_MAGIC, 1,
                                    chardev_stats_t)
#define CHARDEV_IOCTL_SETSIZE  _IOW(CHARDEV_MAGIC, 2,
                                    unsigned int)

#define CHARDEV_IOC_MAXNR  2

