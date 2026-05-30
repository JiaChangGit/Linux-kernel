#ifndef _CHARDEV_H
#define _CHARDEV_H

#include <linux/ioctl.h>

/* ioctl magic number：用來辨識 command 是否屬於本 driver。 */
#define CHARDEV_MAGIC 'k'

/* ioctl commands：
 *  - _IO  ：沒有額外資料，只傳 command。
 *  - _IOR ：driver 將資料回傳到 userspace。
 *  - _IOW ：userspace 將資料傳入 driver。
 */
#define IOCTL_RESET_BUF _IO(CHARDEV_MAGIC, 0)        /* 清空 kernel buffer。 */
#define IOCTL_GET_LEN _IOR(CHARDEV_MAGIC, 1, int)    /* 取得目前資料長度。 */
#define IOCTL_SET_RDONLY _IOW(CHARDEV_MAGIC, 2, int) /* 設定唯讀模式。 */

/* 目前最大的 ioctl command number，用於 chardev_ioctl() 做基本檢查。 */
#define CHARDEV_MAGIC_MAX 2

#endif /* _CHARDEV_H */
