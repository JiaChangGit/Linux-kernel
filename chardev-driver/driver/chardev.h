#ifndef _CHARDEV_H
#define _CHARDEV_H

#include <linux/ioctl.h>

/* ioctl magic number，選一個不與系統衝突的值 */
#define CHARDEV_MAGIC 'k'

/* 定義 ioctl 命令 */
#define IOCTL_RESET_BUF _IO(CHARDEV_MAGIC, 0)        /* 清空 buffer */
#define IOCTL_GET_LEN _IOR(CHARDEV_MAGIC, 1, int)    /* 取得資料長度 */
#define IOCTL_SET_RDONLY _IOW(CHARDEV_MAGIC, 2, int) /* 設定唯讀模式 */

#define CHARDEV_MAGIC_MAX 2

#endif /* _CHARDEV_H */
