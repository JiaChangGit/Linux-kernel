// SPDX-License-Identifier: GPL-2.0
/*
 * shm_module.c  —  Shared Memory IPC kernel module
 *
 * Mechanism : vmalloc() ring-buffer exposed via mmap()
 * Device    : /dev/shm_ipc   (character device)
 * Stats     : /proc/shm_stats
 *
 * Two intentionally distinct data paths are provided:
 *
 *  [syscall path]  write() / read()
 *    write(): copy_from_user → ring-buf   (1 copy, spinlock)
 *    read() : ring-buf → copy_to_user     (1 copy, spinlock)
 *    Same copy count as MQ — isolates queue-mechanism overhead.
 *
 *  [mmap / zero-copy path]
 *    mmap() maps the SAME physical pages into the calling process.
 *    Producer and consumer write/read directly — zero extra copies,
 *    zero syscalls per message.  Only a memory barrier per slot.
 *    This is the core performance advantage of shared memory.
 *
 * Ring-buffer layout  (struct shm_region — mirrored in user/common.h)
 *   head : next write index  (producer owns this field)
 *   tail : next read  index  (consumer owns this field)
 *   Full : (head+1) % CAP == tail
 *   Empty: head == tail
 *
 * Tested on Ubuntu 24.04 / Linux 6.8
 *   class_create(name)         — single-arg form (kernel >= 6.4)
 *   vm_flags_set(vma, flags)   — new vma-flags API (kernel >= 6.3)
 *   vmalloc_to_pfn()           — page-by-page pfn walk for remap_pfn_range
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("JIA");
MODULE_DESCRIPTION("IPC demo: Shared Memory via vmalloc ring-buffer + mmap");
MODULE_VERSION("1.0");

/* ───────────────────── tunables (mirror in user/common.h) ──────────── */
#define SHM_DEVICE     "shm_ipc"
#define MSG_SIZE       64
#define RING_CAPACITY  512    /* slots; keep as power-of-two             */

/*
 * shm_region  —  the shared layout
 * Pad head/tail onto separate cache lines (64 B) so producer and consumer
 * don't cause false-sharing on the same cache line.
 */
struct __attribute__((aligned(64))) shm_region {

    volatile uint32_t head;
    uint8_t pad1[60];

    volatile uint32_t tail;
    uint8_t pad2[60];

    uint32_t capacity;
    uint32_t msg_size;

    char data[RING_CAPACITY][MSG_SIZE];
};

/* size to expose via mmap — must be page-aligned */
#define SHM_BUF_SIZE   PAGE_ALIGN(sizeof(struct shm_region))

static struct shm_region *g_shm;     /* vmalloc'd, page-aligned           */
static spinlock_t         g_spin;    /* guards syscall path               */

/* ───────────────────────── statistics ──────────────────────────────── */
static atomic64_t st_wr;
static atomic64_t st_rd;
static atomic64_t st_lat_ns_total;
static ktime_t    st_last_wr_ts;

/* ───────────────────────── char-device ─────────────────────────────── */
static dev_t         g_devno;
static struct cdev   g_cdev;
static struct class *g_class;

/* ─── file operations ────────────────────────────────────────────────── */
static int shm_open   (struct inode *n, struct file *f) { return 0; }
static int shm_release(struct inode *n, struct file *f) { return 0; }

/* syscall write — copy_from_user into ring slot (spinlock) */
static ssize_t shm_write(struct file *f, const char __user *ubuf,
                          size_t len, loff_t *pos)
{
    uint32_t head, next;
    ktime_t  ts;

    if (len > MSG_SIZE) len = MSG_SIZE;

    spin_lock(&g_spin);
    head = g_shm->head;
    next = (head + 1) % RING_CAPACITY;
    if (next == g_shm->tail) {           /* ring full  */
        spin_unlock(&g_spin);
        return -ENOSPC;
    }
    if (copy_from_user(g_shm->data[head], ubuf, len)) {
        spin_unlock(&g_spin);
        return -EFAULT;
    }
    ts = ktime_get();
    smp_wmb();                           /* data before head update */
    g_shm->head = next;
    spin_unlock(&g_spin);

    atomic64_inc(&st_wr);
    WRITE_ONCE(st_last_wr_ts, ts);
    return (ssize_t)MSG_SIZE;
}

/* syscall read — ring slot into copy_to_user (spinlock) */
static ssize_t shm_read(struct file *f, char __user *ubuf,
                         size_t len, loff_t *pos)
{
    uint32_t tail;
    ktime_t  ts;
    s64      lat;
    char     tmp[MSG_SIZE];

    if (len < MSG_SIZE) return -EINVAL;

    spin_lock(&g_spin);
    if (g_shm->head == g_shm->tail) {   /* ring empty */
        spin_unlock(&g_spin);
        return -EAGAIN;
    }
    tail = g_shm->tail;
    ts   = ktime_get();
    smp_rmb();
    memcpy(tmp, g_shm->data[tail], MSG_SIZE);
    g_shm->tail = (tail + 1) % RING_CAPACITY;
    spin_unlock(&g_spin);

    if (copy_to_user(ubuf, tmp, MSG_SIZE))
        return -EFAULT;

    lat = ktime_to_ns(ktime_sub(ts, READ_ONCE(st_last_wr_ts)));
    if (lat > 0) atomic64_add(lat, &st_lat_ns_total);
    atomic64_inc(&st_rd);
    return (ssize_t)MSG_SIZE;
}

/*
 * shm_mmap  —  zero-copy path
 *
 * vmalloc pages are not physically contiguous, so we iterate page-by-page
 * with vmalloc_to_pfn() + remap_pfn_range().
 *
 * After this returns, userspace holds a virtual pointer to the same
 * physical frames as g_shm.  Producer and consumer can read/write
 * ring slots directly — no syscall, no copy per message.
 */
static int shm_mmap(struct file *f, struct vm_area_struct *vma)
{
    unsigned long vm_sz = vma->vm_end - vma->vm_start;
    unsigned long uaddr = vma->vm_start;
    unsigned long kaddr = (unsigned long)g_shm;
    unsigned long done  = 0;
    int ret;

    if (vm_sz > SHM_BUF_SIZE)
        return -EINVAL;

    /* kernel >= 6.3: vm_flags_set() replaces direct vma->vm_flags |= */
    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);

    while (done < vm_sz) {
        unsigned long pfn = vmalloc_to_pfn((void *)kaddr);
        ret = remap_pfn_range(vma, uaddr, pfn, PAGE_SIZE, vma->vm_page_prot);
        if (ret) return ret;
        kaddr += PAGE_SIZE;
        uaddr += PAGE_SIZE;
        done  += PAGE_SIZE;
    }
    return 0;
}

static const struct file_operations g_fops = {
    .owner   = THIS_MODULE,
    .open    = shm_open,
    .release = shm_release,
    .write   = shm_write,
    .read    = shm_read,
    .mmap    = shm_mmap,
};

/* ─── /proc/shm_stats ────────────────────────────────────────────────── */
static int shm_stats_show(struct seq_file *m, void *v)
{
    u64      wc   = atomic64_read(&st_wr);
    u64      rc   = atomic64_read(&st_rd);
    u64      lat  = atomic64_read(&st_lat_ns_total);
    uint32_t used = (g_shm->head - g_shm->tail + RING_CAPACITY) % RING_CAPACITY;

    seq_printf(m, "mechanism        : vmalloc ring-buffer (Shared Memory)\n");
    seq_printf(m, "msg_size_bytes   : %d\n",   MSG_SIZE);
    seq_printf(m, "ring_capacity    : %d\n",   RING_CAPACITY);
    seq_printf(m, "mmap_size_bytes  : %lu\n",  SHM_BUF_SIZE);
    seq_printf(m, "write_count      : %llu\n", wc);
    seq_printf(m, "read_count       : %llu\n", rc);
    seq_printf(m, "avg_latency_ns   : %llu\n", rc ? lat / rc : 0ULL);
    seq_printf(m, "ring_used_slots  : %u\n",   used);
    seq_printf(m, "ring_free_slots  : %u\n",   RING_CAPACITY - 1 - used);
    return 0;
}
static int  shm_proc_open(struct inode *i, struct file *f)
            { return single_open(f, shm_stats_show, NULL); }
static const struct proc_ops g_proc_ops = {
    .proc_open    = shm_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ─── init / exit ────────────────────────────────────────────────────── */
static int __init shm_init(void)
{
    int ret;

    g_shm = vmalloc(SHM_BUF_SIZE);
    if (!g_shm) return -ENOMEM;
    memset(g_shm, 0, SHM_BUF_SIZE);
    g_shm->capacity = RING_CAPACITY;
    g_shm->msg_size = MSG_SIZE;
    spin_lock_init(&g_spin);

    atomic64_set(&st_wr, 0);
    atomic64_set(&st_rd, 0);
    atomic64_set(&st_lat_ns_total, 0);

    ret = alloc_chrdev_region(&g_devno, 0, 1, SHM_DEVICE);
    if (ret) { pr_err("shm: alloc_chrdev_region: %d\n", ret); goto err_vfree; }

    cdev_init(&g_cdev, &g_fops);
    ret = cdev_add(&g_cdev, g_devno, 1);
    if (ret) goto err_cdev;

    g_class = class_create(SHM_DEVICE "_class");
    if (IS_ERR(g_class)) { ret = PTR_ERR(g_class); goto err_class; }

    if (!device_create(g_class, NULL, g_devno, NULL, SHM_DEVICE))
        { ret = -ENOMEM; goto err_dev; }

    proc_create("shm_stats", 0444, NULL, &g_proc_ops);

    pr_info("shm_module: /dev/%s ready  (cap=%d  msg=%dB  mmap=%luB)\n",
            SHM_DEVICE, RING_CAPACITY, MSG_SIZE, SHM_BUF_SIZE);
    return 0;

err_dev:   class_destroy(g_class);
err_class: cdev_del(&g_cdev);
err_cdev:  unregister_chrdev_region(g_devno, 1);
err_vfree: vfree(g_shm);
    return ret;
}

static void __exit shm_exit(void)
{
    remove_proc_entry("shm_stats", NULL);
    device_destroy(g_class, g_devno);
    class_destroy(g_class);
    cdev_del(&g_cdev);
    unregister_chrdev_region(g_devno, 1);
    vfree(g_shm);
    pr_info("shm_module: unloaded\n");
}

module_init(shm_init);
module_exit(shm_exit);
