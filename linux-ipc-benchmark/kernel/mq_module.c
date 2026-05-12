// SPDX-License-Identifier: GPL-2.0
/*
 * mq_module.c  —  Message Queue IPC kernel module
 *
 * Mechanism : Linux kfifo (kernel FIFO ring-buffer) + wait-queue blocking
 * Device    : /dev/mq_ipc   (character device)
 * Stats     : /proc/mq_stats
 *
 * Data-copy path (the key educational point):
 *   write()  →  copy_from_user()  →  kfifo   (1st copy: user→kernel)
 *   read()   →  kfifo             →  copy_to_user()  (2nd copy: kernel→user)
 *
 * Every message crosses the user/kernel boundary TWICE.
 * That cost is what shared-memory mmap eliminates entirely.
 *
 * Tested on Ubuntu 24.04 / Linux 6.8  (kernel >= 6.4 API)
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
MODULE_AUTHOR("linux-ipc-benchmark");
MODULE_DESCRIPTION("IPC demo: Message Queue via kfifo");
MODULE_VERSION("1.0");

/* ───────────────────────── tunables ────────────────────────────────── */
#define MQ_DEVICE    "mq_ipc"
#define MSG_SIZE     64          /* fixed message size in bytes            */
#define QUEUE_DEPTH  512         /* slots; FIFO_SIZE must be power-of-two */
#define FIFO_SIZE    (MSG_SIZE * QUEUE_DEPTH)   /* = 32 768 bytes          */

/* ───────────────────────── kfifo + locking ─────────────────────────── */
static DEFINE_KFIFO(g_fifo, char, FIFO_SIZE);
static DEFINE_MUTEX(g_lock);
static DECLARE_WAIT_QUEUE_HEAD(g_rd_wq);   /* consumer blocks here */
static DECLARE_WAIT_QUEUE_HEAD(g_wr_wq);   /* producer blocks here */

/* ───────────────────────── statistics ──────────────────────────────── */
static atomic64_t st_enq;          /* total enqueued messages              */
static atomic64_t st_deq;          /* total dequeued messages              */
static atomic64_t st_lat_ns_total; /* cumulative enq→deq latency (ns)     */
static ktime_t    st_last_enq_ts;  /* ktime of most-recent enqueue         */

/* ───────────────────────── char-device ─────────────────────────────── */
static dev_t         g_devno;
static struct cdev   g_cdev;
static struct class *g_class;

/* ─── file operations ────────────────────────────────────────────────── */
static int mq_open   (struct inode *n, struct file *f) { return 0; }
static int mq_release(struct inode *n, struct file *f) { return 0; }

/*
 * mq_write  —  producer path
 * Blocks (TASK_INTERRUPTIBLE) when kfifo is full; wakes consumer.
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

    /* sleep until there is room for exactly one message */
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
 * mq_read  —  consumer path
 * Blocks unless O_NONBLOCK; records per-message latency.
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

/* ─── /proc/mq_stats ─────────────────────────────────────────────────── */
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

/* ─── init / exit ────────────────────────────────────────────────────── */
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

    /* Linux >= 6.4: class_create() takes only the name */
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
