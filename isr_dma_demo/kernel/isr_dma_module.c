// SPDX-License-Identifier: GPL-2.0
/*
 * isr_dma_module.c — ISR + DMA Ring Buffer Demo (Linux Kernel Module)
 *
 * Demonstrates:
 *   1. Simulated hardware interrupt (via hrtimer) acting as "ISR producer"
 *   2. A DMA-coherent ring buffer shared between kernel and userspace
 *   3. Benchmarking: ISR+DMA path vs. a naive copy path
 *
 * Architecture:
 *   ┌──────────────┐   IRQ/hrtimer   ┌─────────────────────┐
 *   │  Simulated   │ ─────────────►  │  ISR Handler        │
 *   │  HW Device   │                 │  (enqueue to ring)  │
 *   └──────────────┘                 └──────────┬──────────┘
 *                                               │ DMA-coherent memory
 *                                               ▼
 *                                    ┌─────────────────────┐
 *                                    │   Ring Buffer        │
 *                                    │  (shared page map)  │
 *                                    └──────────┬──────────┘
 *                                               │ mmap / read
 *                                               ▼
 *                                    ┌─────────────────────┐
 *                                    │   Userspace App     │
 *                                    └─────────────────────┘
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>

/* ─── Configuration ─────────────────────────────────────────── */
#define DEVICE_NAME     "isr_dma"
#define CLASS_NAME      "isr_dma_class"
#define RING_SLOTS      256          /* must be power of 2          */
#define SLOT_SIZE       64           /* bytes per slot               */
#define RING_TOTAL      (RING_SLOTS * SLOT_SIZE)  /* 16 KiB total   */
#define ISR_PERIOD_NS   500000UL     /* 500 µs → ~2000 IRQ/s        */

/* Benchmark: naive (non-DMA) buffer size matches ring total */
#define NAIVE_BUF_SIZE  RING_TOTAL

/* ─── Ring Buffer Control Block (mapped into shared memory) ──── */
struct ring_ctrl {
    atomic_t head;      /* producer writes here (ISR)   */
    atomic_t tail;      /* consumer reads from here     */
    u32      slots;     /* total number of slots        */
    u32      slot_size; /* bytes per slot               */
    u64      isr_count; /* total interrupts fired       */
    u64      drop_count;/* slots dropped (overflow)     */
};

/* ─── Module-level state ────────────────────────────────────── */
static struct {
    /* Character device bookkeeping */
    dev_t           devno;
    struct cdev     cdev;
    struct class   *cls;
    struct device  *dev;

    /* DMA-coherent ring buffer */
    void           *ring_virt;  /* kernel virtual address  */
    dma_addr_t      ring_dma;   /* bus/DMA address         */
    struct ring_ctrl *ctrl;     /* points into ring_virt   */
    u8             *data;       /* payload area            */

    /* Naive (benchmark comparison) buffer */
    void           *naive_buf;
    spinlock_t      naive_lock;
    size_t          naive_wr;

    /* hrtimer simulates periodic hardware interrupt */
    struct hrtimer  timer;
    bool            timer_running;

    /* Benchmark counters */
    ktime_t         bench_start;
    u64             bench_isr_ops;
    u64             bench_naive_ops;
    u64             bench_isr_ns;     /* total ns spent in ISR path  */
    u64             bench_naive_ns;   /* total ns spent in naive path*/

    /* /proc entry */
    struct proc_dir_entry *proc_entry;

    /* Fake DMA platform device (needed for dma_alloc_coherent) */
    struct platform_device *pdev;
} g;

/* ─── Helpers ────────────────────────────────────────────────── */
static inline u32 ring_next(u32 idx)
{
    return (idx + 1) & (RING_SLOTS - 1);
}

static inline bool ring_full(u32 head, u32 tail)
{
    return ring_next(head) == tail;
}

static inline bool ring_empty(u32 head, u32 tail)
{
    return head == tail;
}

/*
 * isr_produce() — called from hrtimer callback (soft-IRQ context).
 *
 * In a real driver this would be triggered by a hardware interrupt.
 * We write a 64-byte payload into the next ring slot using the
 * DMA-coherent buffer (no cache flush needed — it's uncached).
 */
static void isr_produce(void)
{
    ktime_t t0 = ktime_get();
    u32 head = (u32)atomic_read(&g.ctrl->head);
    u32 tail = (u32)atomic_read(&g.ctrl->tail);

    g.ctrl->isr_count++;

    if (ring_full(head, tail)) {
        g.ctrl->drop_count++;
        return;
    }

    /* Write payload — 64 bytes of timestamp + counter data */
    u8 *slot = g.data + head * SLOT_SIZE;
    u64 ts = (u64)ktime_to_ns(ktime_get());
    memcpy(slot,      &ts,                  8);
    memcpy(slot + 8,  &g.ctrl->isr_count,   8);
    memset(slot + 16, 0xAB, SLOT_SIZE - 16); /* pattern fill */

    /* Advance head — release barrier ensures payload visible first */
    smp_store_release((u32 *)&g.ctrl->head, ring_next(head));

    g.bench_isr_ns  += (u64)ktime_to_ns(ktime_sub(ktime_get(), t0));
    g.bench_isr_ops++;
}

/*
 * naive_produce() — benchmark comparison path.
 *
 * Simulates a traditional copy-based approach: acquires a spinlock,
 * copies data into a vmalloc'd buffer. Represents legacy ISR designs
 * without ring buffers or DMA coherency.
 */
static void naive_produce(void)
{
    ktime_t t0 = ktime_get();
    unsigned long flags;
    u8 tmp[SLOT_SIZE];

    u64 ts = (u64)ktime_to_ns(ktime_get());
    memcpy(tmp,      &ts,                8);
    memset(tmp + 8, 0xCD, SLOT_SIZE - 8);

    spin_lock_irqsave(&g.naive_lock, flags);
    memcpy((u8 *)g.naive_buf + g.naive_wr, tmp, SLOT_SIZE);
    g.naive_wr = (g.naive_wr + SLOT_SIZE) % NAIVE_BUF_SIZE;
    spin_unlock_irqrestore(&g.naive_lock, flags);

    g.bench_naive_ns  += (u64)ktime_to_ns(ktime_sub(ktime_get(), t0));
    g.bench_naive_ops++;
}

/* ─── hrtimer callback (simulated ISR) ──────────────────────── */
static enum hrtimer_restart timer_cb(struct hrtimer *t)
{
    isr_produce();
    naive_produce();

    hrtimer_forward_now(t, ns_to_ktime(ISR_PERIOD_NS));
    return HRTIMER_RESTART;
}

/* ─── /proc/isr_dma_stats ────────────────────────────────────── */
static int stats_show(struct seq_file *m, void *v)
{
    u64 isr_avg  = g.bench_isr_ops  ? g.bench_isr_ns  / g.bench_isr_ops  : 0;
    u64 naive_avg= g.bench_naive_ops? g.bench_naive_ns / g.bench_naive_ops: 0;

    seq_printf(m, "=== ISR + DMA Ring Buffer — Kernel Stats ===\n");
    seq_printf(m, "isr_count       : %llu\n", g.ctrl->isr_count);
    seq_printf(m, "drop_count      : %llu\n", g.ctrl->drop_count);
    seq_printf(m, "ring_head       : %d\n",   atomic_read(&g.ctrl->head));
    seq_printf(m, "ring_tail       : %d\n",   atomic_read(&g.ctrl->tail));
    seq_printf(m, "\n--- Benchmark (per-operation average) ---\n");
    seq_printf(m, "isr_dma_ops     : %llu\n", g.bench_isr_ops);
    seq_printf(m, "isr_dma_avg_ns  : %llu\n", isr_avg);
    seq_printf(m, "naive_ops       : %llu\n", g.bench_naive_ops);
    seq_printf(m, "naive_avg_ns    : %llu\n", naive_avg);
    if (naive_avg && isr_avg)
        seq_printf(m, "speedup_x       : %llu.%02llu\n",
                   naive_avg / isr_avg,
                   (naive_avg * 100 / isr_avg) % 100);
    return 0;
}

static int stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, stats_show, NULL);
}

static const struct proc_ops stats_ops = {
    .proc_open    = stats_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ─── Character device file_operations ──────────────────────── */

/* open: start the hrtimer "ISR" on first open */
static int dev_open(struct inode *inode, struct file *file)
{
    if (!g.timer_running) {
        g.bench_start    = ktime_get();
        g.bench_isr_ops  = 0;
        g.bench_naive_ops= 0;
        g.bench_isr_ns   = 0;
        g.bench_naive_ns = 0;
        g.ctrl->isr_count  = 0;
        g.ctrl->drop_count = 0;
        atomic_set(&g.ctrl->head, 0);
        atomic_set(&g.ctrl->tail, 0);

        hrtimer_start(&g.timer, ns_to_ktime(ISR_PERIOD_NS),
                      HRTIMER_MODE_REL);
        g.timer_running = true;
        pr_info("isr_dma: timer/ISR started (%lu ns period)\n",
                ISR_PERIOD_NS);
    }
    return 0;
}

/* release: stop the timer when last fd closed */
static int dev_release(struct inode *inode, struct file *file)
{
    if (g.timer_running) {
        hrtimer_cancel(&g.timer);
        g.timer_running = false;
        pr_info("isr_dma: timer/ISR stopped. isr_count=%llu drops=%llu\n",
                g.ctrl->isr_count, g.ctrl->drop_count);
    }
    return 0;
}

/*
 * read: userspace drains one slot from the ring.
 * Returns SLOT_SIZE bytes or 0 if empty.
 */
static ssize_t dev_read(struct file *file, char __user *buf,
                         size_t count, loff_t *ppos)
{
    u32 head, tail;

    if (count < SLOT_SIZE)
        return -EINVAL;

    /* Spin-wait briefly for data (simple polling demo) */
    int retries = 1000;
    do {
        head = (u32)atomic_read(&g.ctrl->head);
        tail = (u32)atomic_read(&g.ctrl->tail);
        if (!ring_empty(head, tail))
            break;
        cpu_relax();
    } while (--retries);

    if (ring_empty(head, tail))
        return 0;  /* no data */

    u8 *slot = g.data + tail * SLOT_SIZE;
    if (copy_to_user(buf, slot, SLOT_SIZE))
        return -EFAULT;

    /* Advance tail */
    smp_store_release((u32 *)&g.ctrl->tail, ring_next(tail));
    return SLOT_SIZE;
}

/*
 * mmap: map DMA ring buffer into userspace — zero-copy access.
 * The control block (ring_ctrl) + data area are both visible.
 */
static int dev_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    size_t alloc_size = PAGE_ALIGN(sizeof(struct ring_ctrl) + RING_TOTAL);

    if (size > alloc_size)
        return -EINVAL;

    /* Mark as non-cached so userspace sees ISR writes immediately */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);

    if (g.ring_dma) {
        /* DMA-coherent allocations have a bus address we can map directly. */
        if (dma_mmap_coherent(&g.pdev->dev, vma, g.ring_virt, g.ring_dma,
                              alloc_size))
            return -EAGAIN;
    } else {
        /* vmalloc fallback must be mapped with the vmalloc helper. */
        if (remap_vmalloc_range(vma, g.ring_virt, 0))
            return -EAGAIN;
    }

    pr_info("isr_dma: mmap'd %lu bytes to userspace (zero-copy path)\n",
            size);
    return 0;
}

/*
 * ioctl: control commands
 *   0 → reset stats & ring
 *   1 → query slot size (returns SLOT_SIZE)
 */
static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case 0: /* reset */
        atomic_set(&g.ctrl->head, 0);
        atomic_set(&g.ctrl->tail, 0);
        g.ctrl->isr_count  = 0;
        g.ctrl->drop_count = 0;
        g.bench_isr_ops  = 0;
        g.bench_naive_ops= 0;
        g.bench_isr_ns   = 0;
        g.bench_naive_ns = 0;
        pr_info("isr_dma: stats & ring reset\n");
        return 0;
    case 1: /* query slot size */
        return SLOT_SIZE;
    default:
        return -EINVAL;
    }
}

static const struct file_operations dev_fops = {
    .owner          = THIS_MODULE,
    .open           = dev_open,
    .release        = dev_release,
    .read           = dev_read,
    .mmap           = dev_mmap,
    .unlocked_ioctl = dev_ioctl,
};

/* ─── Module init / exit ─────────────────────────────────────── */
static int __init isr_dma_init(void)
{
    int ret;
    size_t alloc_size = PAGE_ALIGN(sizeof(struct ring_ctrl) + RING_TOTAL);

    pr_info("isr_dma: loading — ring slots=%d slot_size=%d total=%zu\n",
            RING_SLOTS, SLOT_SIZE, alloc_size);

    /* 1. Register char device */
    ret = alloc_chrdev_region(&g.devno, 0, 1, DEVICE_NAME);
    if (ret < 0) { pr_err("alloc_chrdev_region failed\n"); return ret; }

    cdev_init(&g.cdev, &dev_fops);
    g.cdev.owner = THIS_MODULE;
    ret = cdev_add(&g.cdev, g.devno, 1);
    if (ret) { pr_err("cdev_add failed\n"); goto err_chrdev; }

    g.cls = class_create(CLASS_NAME);
    if (IS_ERR(g.cls)) { ret = PTR_ERR(g.cls); goto err_cdev; }

    g.dev = device_create(g.cls, NULL, g.devno, NULL, DEVICE_NAME);
    if (IS_ERR(g.dev)) { ret = PTR_ERR(g.dev); goto err_class; }

    /* 2. Create a dummy platform device so we can call dma_alloc_coherent */
    g.pdev = platform_device_alloc("isr_dma_pdev", 0);
    if (!g.pdev) { ret = -ENOMEM; goto err_device; }
    ret = platform_device_add(g.pdev);
    if (ret) { goto err_pdev_alloc; }

    /* Set the DMA mask on the platform device */
    ret = dma_set_mask_and_coherent(&g.pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        pr_warn("isr_dma: dma_set_mask failed (%d), using vmalloc fallback\n",
                ret);
        /* Fallback: use vmalloc (not truly DMA-coherent, but functional) */
        g.ring_virt = vzalloc(alloc_size);
        if (!g.ring_virt) { ret = -ENOMEM; goto err_pdev_add; }
        g.ring_dma = 0; /* mark as vmalloc */
    } else {
        g.ring_virt = dma_alloc_coherent(&g.pdev->dev, alloc_size,
                                          &g.ring_dma, GFP_KERNEL);
        if (!g.ring_virt) {
            pr_warn("isr_dma: dma_alloc_coherent failed, falling back to vmalloc\n");
            g.ring_virt = vzalloc(alloc_size);
            if (!g.ring_virt) { ret = -ENOMEM; goto err_pdev_add; }
            g.ring_dma = 0;
        }
    }

    /* Lay out control block + data inside the allocation */
    g.ctrl = (struct ring_ctrl *)g.ring_virt;
    g.data = (u8 *)g.ring_virt + sizeof(struct ring_ctrl);

    /* Align data to SLOT_SIZE boundary */
    uintptr_t data_off = ALIGN(sizeof(struct ring_ctrl), SLOT_SIZE);
    g.data = (u8 *)g.ring_virt + data_off;

    atomic_set(&g.ctrl->head, 0);
    atomic_set(&g.ctrl->tail, 0);
    g.ctrl->slots     = RING_SLOTS;
    g.ctrl->slot_size = SLOT_SIZE;
    g.ctrl->isr_count  = 0;
    g.ctrl->drop_count = 0;

    /* 3. Naive buffer (vmalloc — represents legacy approach) */
    g.naive_buf = vzalloc(NAIVE_BUF_SIZE);
    if (!g.naive_buf) { ret = -ENOMEM; goto err_ring; }
    spin_lock_init(&g.naive_lock);
    g.naive_wr = 0;

    /* 4. hrtimer (simulates periodic hardware interrupt) */
    hrtimer_setup(&g.timer, timer_cb, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    g.timer_running  = false;

    /* 5. /proc entry */
    g.proc_entry = proc_create("isr_dma_stats", 0444, NULL, &stats_ops);
    if (!g.proc_entry)
        pr_warn("isr_dma: could not create /proc/isr_dma_stats\n");

    pr_info("isr_dma: loaded — /dev/%s ready, /proc/isr_dma_stats ready\n",
            DEVICE_NAME);
    pr_info("isr_dma: ring @ virt=%p dma=%pad size=%zu\n",
            g.ring_virt, &g.ring_dma, alloc_size);
    return 0;

err_ring:
    if (g.ring_dma)
        dma_free_coherent(&g.pdev->dev, alloc_size, g.ring_virt, g.ring_dma);
    else
        vfree(g.ring_virt);
err_pdev_add:
    platform_device_del(g.pdev);
err_pdev_alloc:
    platform_device_put(g.pdev);
err_device:
    device_destroy(g.cls, g.devno);
err_class:
    class_destroy(g.cls);
err_cdev:
    cdev_del(&g.cdev);
err_chrdev:
    unregister_chrdev_region(g.devno, 1);
    return ret;
}

static void __exit isr_dma_exit(void)
{
    size_t alloc_size = PAGE_ALIGN(sizeof(struct ring_ctrl) + RING_TOTAL);

    if (g.proc_entry)
        proc_remove(g.proc_entry);

    if (g.timer_running)
        hrtimer_cancel(&g.timer);

    vfree(g.naive_buf);

    if (g.ring_dma)
        dma_free_coherent(&g.pdev->dev, alloc_size, g.ring_virt, g.ring_dma);
    else
        vfree(g.ring_virt);

    platform_device_del(g.pdev);
    platform_device_put(g.pdev);
    device_destroy(g.cls, g.devno);
    class_destroy(g.cls);
    cdev_del(&g.cdev);
    unregister_chrdev_region(g.devno, 1);

    pr_info("isr_dma: unloaded\n");
}

module_init(isr_dma_init);
module_exit(isr_dma_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("ISR-DMA Demo");
MODULE_DESCRIPTION("ISR + DMA Ring Buffer demo with benchmark comparison");
MODULE_VERSION("1.0");
