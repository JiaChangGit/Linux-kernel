// SPDX-License-Identifier: GPL-2.0
/*
 * mq_module.c - 訊息佇列 IPC kernel module
 *
 * 機制：Linux kfifo（核心端 FIFO ring buffer）+ wait queue 阻塞等待。
 * 裝置：/dev/mq_ipc，使用 character device 介面提供 read/write。
 * 觀測：/proc/mq_stats，輸出 enqueue/dequeue 次數與 FIFO 使用量。
 *
 * 資料路徑：
 *   write() -> copy_from_user() -> kfifo
 *              第一次複製：user buffer 到 kernel buffer。
 *   read()  -> kfifo -> copy_to_user()
 *              第二次複製：kernel buffer 到 user buffer。
 *
 * 重點：每筆訊息都會跨越 user/kernel boundary 兩次。
 * 這正是後續 mmap shared memory 路徑想要避開的成本。
 *
 * 測試環境：Ubuntu 24.04 / Linux 6.8，使用 kernel >= 6.4 的 API 寫法。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kfifo.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/wait.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JIA");
MODULE_DESCRIPTION("IPC demo: Message Queue via kfifo");
MODULE_VERSION("1.0");

/* ───────────────────────── 可調整參數 ─────────────────────────────── */
#define MQ_DEVICE    "mq_ipc"
#define MSG_SIZE     64          /* 每筆訊息固定 64 bytes。 */
#define QUEUE_DEPTH  512         /* FIFO slot 數量；總容量需適合 kfifo 使用。 */
#define FIFO_SIZE    (MSG_SIZE * QUEUE_DEPTH)   /* FIFO 總容量：32768 bytes。 */

/* ───────────────────────── FIFO 與同步物件 ─────────────────────────── */
static DEFINE_KFIFO(g_fifo, char, FIFO_SIZE);
static DEFINE_MUTEX(g_lock);
static DECLARE_WAIT_QUEUE_HEAD(g_rd_wq);   /* FIFO 沒資料時，consumer 在這裡睡眠。 */
static DECLARE_WAIT_QUEUE_HEAD(g_wr_wq);   /* FIFO 沒空間時，producer 在這裡睡眠。 */

/* ───────────────────────── 統計資料 ───────────────────────────────── */
static atomic64_t st_enq;          /* 已寫入 FIFO 的訊息總數。 */
static atomic64_t st_deq;          /* 已從 FIFO 讀出的訊息總數。 */
static atomic64_t st_lat_ns_total; /* enqueue 到 dequeue 的累積延遲，單位 ns。 */
static ktime_t    st_last_enq_ts;  /* 最近一次 enqueue 的時間戳。 */

/* ───────────────────────── character device 註冊狀態 ──────────────── */
static dev_t         g_devno;
static struct cdev   g_cdev;
static struct class *g_class;

/* ─── VFS file operations：把 /dev/mq_ipc 的操作接到本模組 ─────────── */
static int mq_open   (struct inode *n, struct file *f) { return 0; }
static int mq_release(struct inode *n, struct file *f) { return 0; }

/*
 * mq_write - producer 寫入路徑。
 *
 * 流程：
 *   1. 先用 copy_from_user() 把 user buffer 複製到 kernel stack。
 *   2. FIFO 滿時進入 wait queue，等 consumer 讀走資料。
 *   3. FIFO 有空間後，用 kfifo_in() 寫入固定 64 bytes。
 *   4. 喚醒可能正在等待資料的 consumer。
 */
static ssize_t mq_write(struct file *f, const char __user *ubuf,
                         size_t len, loff_t *pos)
{
    char    kb[MSG_SIZE];
    int     ret;
    ktime_t ts;

    if (len > MSG_SIZE) len = MSG_SIZE;
    if (copy_from_user(kb, ubuf, len))
        return -EFAULT;

    /* 等到 FIFO 至少有一筆訊息的空間，避免寫入半筆資料。 */
    ret = wait_event_interruptible(g_wr_wq,
                                   kfifo_avail(&g_fifo) >= MSG_SIZE);
    if (ret) return -EINTR;

    mutex_lock(&g_lock);
    ts = ktime_get();
    kfifo_in(&g_fifo, kb, MSG_SIZE);
    mutex_unlock(&g_lock);

    atomic64_inc(&st_enq);
    WRITE_ONCE(st_last_enq_ts, ts);

    wake_up_interruptible(&g_rd_wq);
    return (ssize_t)MSG_SIZE;
}

/*
 * mq_read - consumer 讀取路徑。
 *
 * 流程：
 *   1. FIFO 沒資料時，blocking fd 會睡眠；non-blocking fd 回 -EAGAIN。
 *   2. 用 kfifo_out() 取出固定 64 bytes。
 *   3. 用 copy_to_user() 複製回 user buffer。
 *   4. 更新統計並喚醒可能正在等待空間的 producer。
 */
static ssize_t mq_read(struct file *f, char __user *ubuf,
                        size_t len, loff_t *pos)
{
    char    kb[MSG_SIZE];
    int     ret;
    ktime_t ts;
    s64     lat;

    if (len < MSG_SIZE) return -EINVAL;

    if (f->f_flags & O_NONBLOCK) {
        if (kfifo_len(&g_fifo) < MSG_SIZE)
            return -EAGAIN;
    } else {
        ret = wait_event_interruptible(g_rd_wq,
                                       kfifo_len(&g_fifo) >= MSG_SIZE);
        if (ret) return -EINTR;
    }

    mutex_lock(&g_lock);
    if (kfifo_out(&g_fifo, kb, MSG_SIZE) != MSG_SIZE) {
        mutex_unlock(&g_lock);
        return -EAGAIN;
    }
    ts = ktime_get();
    mutex_unlock(&g_lock);

    if (copy_to_user(ubuf, kb, MSG_SIZE))
        return -EFAULT;

    lat = ktime_to_ns(ktime_sub(ts, READ_ONCE(st_last_enq_ts)));
    if (lat > 0)
        atomic64_add(lat, &st_lat_ns_total);
    atomic64_inc(&st_deq);

    wake_up_interruptible(&g_wr_wq);
    return (ssize_t)MSG_SIZE;
}

static const struct file_operations g_fops = {
    .owner   = THIS_MODULE,
    .open    = mq_open,
    .release = mq_release,
    .write   = mq_write,
    .read    = mq_read,
};

/* ─── /proc/mq_stats：提供簡單文字統計，方便 cat/watch 觀察 ───────── */
static int mq_stats_show(struct seq_file *m, void *v)
{
    u64 enq = atomic64_read(&st_enq);
    u64 deq = atomic64_read(&st_deq);
    u64 lat = atomic64_read(&st_lat_ns_total);

    seq_printf(m, "mechanism        : kfifo Message Queue\n");
    seq_printf(m, "msg_size_bytes   : %d\n",   MSG_SIZE);
    seq_printf(m, "queue_depth      : %d\n",   QUEUE_DEPTH);
    seq_printf(m, "enqueue_count    : %llu\n", enq);
    seq_printf(m, "dequeue_count    : %llu\n", deq);
    seq_printf(m, "avg_latency_ns   : %llu\n", deq ? lat / deq : 0ULL);
    seq_printf(m, "fifo_used_bytes  : %u\n",   kfifo_len(&g_fifo));
    seq_printf(m, "fifo_free_bytes  : %u\n",   kfifo_avail(&g_fifo));
    return 0;
}
static int  mq_proc_open(struct inode *i, struct file *f)
            { return single_open(f, mq_stats_show, NULL); }
static const struct proc_ops g_proc_ops = {
    .proc_open    = mq_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ─── module init / exit：註冊與釋放 /dev、/proc 資源 ──────────────── */
static int __init mq_init(void)
{
    int ret;

    atomic64_set(&st_enq, 0);
    atomic64_set(&st_deq, 0);
    atomic64_set(&st_lat_ns_total, 0);

    ret = alloc_chrdev_region(&g_devno, 0, 1, MQ_DEVICE);
    if (ret) { pr_err("mq: alloc_chrdev_region: %d\n", ret); return ret; }

    cdev_init(&g_cdev, &g_fops);
    ret = cdev_add(&g_cdev, g_devno, 1);
    if (ret) goto err_cdev;

    /* Linux >= 6.4：class_create() 只需要傳入 class 名稱。 */
    g_class = class_create(MQ_DEVICE "_class");
    if (IS_ERR(g_class)) { ret = PTR_ERR(g_class); goto err_class; }

    if (!device_create(g_class, NULL, g_devno, NULL, MQ_DEVICE))
        { ret = -ENOMEM; goto err_dev; }

    proc_create("mq_stats", 0444, NULL, &g_proc_ops);

    pr_info("mq_module: /dev/%s ready  (depth=%d  msg=%dB  fifo=%dB)\n",
            MQ_DEVICE, QUEUE_DEPTH, MSG_SIZE, FIFO_SIZE);
    return 0;

err_dev:   class_destroy(g_class);
err_class: cdev_del(&g_cdev);
err_cdev:  unregister_chrdev_region(g_devno, 1);
    return ret;
}

static void __exit mq_exit(void)
{
    remove_proc_entry("mq_stats", NULL);
    device_destroy(g_class, g_devno);
    class_destroy(g_class);
    cdev_del(&g_cdev);
    unregister_chrdev_region(g_devno, 1);
    pr_info("mq_module: unloaded\n");
}

module_init(mq_init);
module_exit(mq_exit);
