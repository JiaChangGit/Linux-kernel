// SPDX-License-Identifier: GPL-2.0
/*
 * shm_module.c - 共享記憶體 IPC kernel module
 *
 * 機制：用 vmalloc() 建立核心端 ring buffer，再提供兩種存取方式。
 * 裝置：/dev/shm_ipc，使用 character device 介面。
 * 觀測：/proc/shm_stats，輸出 ring 使用量與 syscall path 統計。
 *
 * 本模組刻意保留兩條資料路徑，用來比較成本來源：
 *
 *  [syscall path] write() / read()
 *    write(): copy_from_user() -> ring buffer。
 *    read() : ring buffer -> copy_to_user()。
 *    這條路徑仍有 user/kernel 複製，方便和 MQ syscall path 對照。
 *
 *  [mmap / zero-copy path]
 *    mmap() 將同一份 backing pages 映射到 user space。
 *    producer / consumer 直接讀寫 mapped ring，每筆訊息不再進 kernel。
 *    正確性依賴 head/tail 協定與 memory barrier。
 *
 * Ring buffer 規則：
 *   head : 下一個寫入 slot，由 producer 更新。
 *   tail : 下一個讀取 slot，由 consumer 更新。
 *   Full : (head + 1) % RING_CAPACITY == tail。
 *   Empty: head == tail。
 *
 * 測試環境：Ubuntu 24.04 / Linux 6.8。
 *   class_create(name)       : kernel >= 6.4 的單參數寫法。
 *   vm_flags_set(vma, flags) : kernel >= 6.3 的 VMA flags helper。
 *   vmalloc_to_pfn()         : vmalloc page 需逐頁轉成 PFN 後映射。
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

/* ───────────────────── 可調整參數，需與 user/common.h 同步 ───────── */
#define SHM_DEVICE     "shm_ipc"
#define MSG_SIZE       64
#define RING_CAPACITY  512    /* ring slot 數量；維持 2 的次方較好計算。 */

/*
 * shm_region - kernel 端共享區布局。
 *
 * head 與 tail 更新頻率高，分開放到不同 cache line 可降低 false sharing。
 * 若調整欄位或 padding，user/common.h 的 shm_region_t 也必須同步檢查。
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

/* mmap 暴露給 user 的大小必須 page-aligned。 */
#define SHM_BUF_SIZE   PAGE_ALIGN(sizeof(struct shm_region))

static struct shm_region *g_shm;     /* vmalloc() 配置的共享 ring buffer。 */
static spinlock_t         g_spin;    /* 保護 syscall path 的 head/tail/data。 */

/* ───────────────────────── 統計資料 ───────────────────────────────── */
static atomic64_t st_wr;
static atomic64_t st_rd;
static atomic64_t st_lat_ns_total;
static ktime_t    st_last_wr_ts;

/* ───────────────────────── character device 註冊狀態 ──────────────── */
static dev_t         g_devno;
static struct cdev   g_cdev;
static struct class *g_class;

/* ─── VFS file operations：/dev/shm_ipc 的 read/write/mmap 入口 ────── */
static int shm_open   (struct inode *n, struct file *f) { return 0; }
static int shm_release(struct inode *n, struct file *f) { return 0; }

/* syscall write：把 user buffer 複製到 ring slot，成功後推進 head。 */
static ssize_t shm_write(struct file *f, const char __user *ubuf,
                          size_t len, loff_t *pos)
{
    uint32_t head, next;
    ktime_t  ts;

    if (len > MSG_SIZE) len = MSG_SIZE;

    spin_lock(&g_spin);
    head = g_shm->head;
    next = (head + 1) % RING_CAPACITY;
    if (next == g_shm->tail) {           /* ring 滿了，讓 caller 稍後重試。 */
        spin_unlock(&g_spin);
        return -ENOSPC;
    }
    if (copy_from_user(g_shm->data[head], ubuf, len)) {
        spin_unlock(&g_spin);
        return -EFAULT;
    }
    ts = ktime_get();
    smp_wmb();                           /* 先讓資料可見，再更新 head。 */
    g_shm->head = next;
    spin_unlock(&g_spin);

    atomic64_inc(&st_wr);
    WRITE_ONCE(st_last_wr_ts, ts);
    return (ssize_t)MSG_SIZE;
}

/* syscall read：從 ring slot 取出資料，再複製回 user buffer。 */
static ssize_t shm_read(struct file *f, char __user *ubuf,
                         size_t len, loff_t *pos)
{
    uint32_t tail;
    ktime_t  ts;
    s64      lat;
    char     tmp[MSG_SIZE];

    if (len < MSG_SIZE) return -EINVAL;

    spin_lock(&g_spin);
    if (g_shm->head == g_shm->tail) {   /* ring 空了，caller 可稍後重試。 */
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
 * shm_mmap - 建立 zero-copy 路徑。
 *
 * vmalloc() 只保證 kernel 虛擬位址連續，不保證實體頁連續。
 * 因此這裡逐頁呼叫 vmalloc_to_pfn()，再用 remap_pfn_range()
 * 將每個 page frame 映射到 user VMA。
 *
 * 成功後，user space 取得指向同一份 backing pages 的虛擬位址。
 * 之後每筆訊息可直接讀寫 shared ring，不必再走 read/write syscall。
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

    /* kernel >= 6.3：使用 vm_flags_set()，不要直接改 vma->vm_flags。 */
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

/* ─── /proc/shm_stats：輸出 shared ring 狀態與 syscall path 統計 ───── */
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

/* ─── module init / exit：配置 shared ring，註冊與釋放 /dev、/proc ── */
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
