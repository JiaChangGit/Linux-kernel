// chardev.c - Custom Character Device Driver
// open/read/write/ioctl + /proc + /sys

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include "chardev.h"

#define DEVICE_NAME  "chardev"
#define CLASS_NAME   "chardev_class"
#define DEFAULT_BUF_SIZE  4096

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KernelDemo");
MODULE_VERSION("1.0");

/* ── 裝置全域狀態 ── */
static dev_t          dev_num;
static struct cdev    my_cdev;
static struct class  *my_class;
static struct device *my_device;

static char         *dev_buffer;
static unsigned int  buf_size = DEFAULT_BUF_SIZE;
static unsigned int  buf_used = 0;
static unsigned long read_count  = 0;
static unsigned long write_count = 0;

static DEFINE_MUTEX(dev_mutex);
static struct proc_dir_entry *proc_entry;

/* ════ file_operations ════ */

static int chardev_open(struct inode *inode,
                        struct file  *filp) {
    pr_info("chardev: open() pid=%d
", current->pid);
    return 0;
}

static ssize_t chardev_read(struct file *filp,
    char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t ret;
    if (mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;
    if (*f_pos >= buf_used) { ret = 0; goto out; }
    count = min(count, (size_t)(buf_used - *f_pos));
    if (copy_to_user(buf, dev_buffer + *f_pos, count))
        { ret = -EFAULT; goto out; }
    *f_pos += count; read_count++; ret = count;
out:
    mutex_unlock(&dev_mutex);
    return ret;
}

static ssize_t chardev_write(struct file *filp,
    const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t ret;
    if (mutex_lock_interruptible(&dev_mutex))
        return -ERESTARTSYS;
    if (count > buf_size) { ret = -ENOSPC; goto out; }
    if (copy_from_user(dev_buffer, buf, count))
        { ret = -EFAULT; goto out; }
    buf_used = count; *f_pos = count;
    write_count++; ret = count;
out:
    mutex_unlock(&dev_mutex);
    return ret;
}

static long chardev_ioctl(struct file *filp,
    unsigned int cmd, unsigned long arg)
{
    chardev_stats_t stats;
    unsigned int new_size; char *new_buf;
    if (_IOC_TYPE(cmd) != CHARDEV_MAGIC)    return -ENOTTY;
    if (_IOC_NR(cmd) > CHARDEV_IOC_MAXNR)  return -ENOTTY;
    switch (cmd) {
    case CHARDEV_IOCTL_CLEAR:
        mutex_lock(&dev_mutex);
        memset(dev_buffer, 0, buf_size); buf_used = 0;
        mutex_unlock(&dev_mutex); break;
    case CHARDEV_IOCTL_GETSTATS:
        stats.read_count  = read_count;
        stats.write_count = write_count;
        stats.buffer_used = buf_used;
        stats.buffer_size = buf_size;
        if (copy_to_user((chardev_stats_t __user *)arg,
                         &stats, sizeof(stats)))
            return -EFAULT; break;
    case CHARDEV_IOCTL_SETSIZE:
        if (copy_from_user(&new_size,
            (unsigned int __user *)arg, sizeof(new_size)))
            return -EFAULT;
        if (new_size == 0 || new_size > 1024*1024)
            return -EINVAL;
        new_buf = krealloc(dev_buffer, new_size, GFP_KERNEL);
        if (!new_buf) return -ENOMEM;
        mutex_lock(&dev_mutex);
        dev_buffer = new_buf; buf_size = new_size;
        mutex_unlock(&dev_mutex); break;
    default: return -ENOTTY;
    }
    return 0;
}

static const struct file_operations chardev_fops = {
    .owner          = THIS_MODULE,
    .open           = chardev_open,
    .release        = chardev_release,
    .read           = chardev_read,
    .write          = chardev_write,
    .unlocked_ioctl = chardev_ioctl,
};

/* ════ procfs /proc/chardev_status ════ */

static int chardev_proc_show(struct seq_file *m, void *v) {
    seq_printf(m, "buffer_size  : %u
", buf_size);
    seq_printf(m, "buffer_used  : %u
", buf_used);
    seq_printf(m, "read_count   : %lu
", read_count);
    seq_printf(m, "write_count  : %lu
", write_count);
    return 0;
}

/* ════ sysfs attributes (DEVICE_ATTR_RO) ════ */

static ssize_t buffer_size_show(...) {
    return sysfs_emit(buf, "%u
", buf_size); }
static ssize_t buffer_used_show(...) {
    return sysfs_emit(buf, "%u
", buf_used); }
static ssize_t read_count_show(...)  {
    return sysfs_emit(buf, "%lu
", read_count); }
static ssize_t write_count_show(...) {
    return sysfs_emit(buf, "%lu
", write_count); }
static ssize_t buffer_content_show(...) { ... }

DEVICE_ATTR_RO(buffer_size);   DEVICE_ATTR_RO(buffer_used);
DEVICE_ATTR_RO(read_count);    DEVICE_ATTR_RO(write_count);
DEVICE_ATTR_RO(buffer_content);
ATTRIBUTE_GROUPS(chardev);

/* ════ module init / exit ════ */

static int __init chardev_init(void) {
    dev_buffer = kzalloc(DEFAULT_BUF_SIZE, GFP_KERNEL);
    alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    cdev_init(&my_cdev, &chardev_fops);
    cdev_add(&my_cdev, dev_num, 1);
    my_class = class_create(CLASS_NAME);   // Linux 6.4+
    my_class->dev_groups = chardev_groups;
    my_device = device_create(my_class, NULL, dev_num,
                               NULL, DEVICE_NAME "0");
    proc_entry = proc_create("chardev_status", 0444,
                              NULL, &chardev_proc_ops);
    return 0;
}
module_init(chardev_init);
