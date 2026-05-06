/**
 * chardev.c - Custom Character Device Driver
 *
 * 功能：
 *  - cdev 字元裝置：open / read / write / ioctl / release
 *  - procfs：/proc/chardev_info  (只讀，顯示驅動狀態)
 *  - sysfs：/sys/class/chardev/chardev0/
 *              ├── buf_len   (可讀：目前 buffer 長度)
 *              ├── read_only (可讀寫：切換唯讀模式)
 *              └── stats     (可讀：open/read/write 計數)
 */

#include "chardev.h"

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/* ===== 常數定義 ===== */
#define DRIVER_NAME "chardev"
#define CLASS_NAME "chardev"
#define BUF_SIZE 4096
#define PROC_ENTRY_NAME "chardev_info"

/* ===== 驅動全域狀態 ===== */
static struct {
  /* kernel buffer */
  char* buf;
  int buf_len;
  int read_only;

  /* 統計計數器 */
  atomic_t open_count;
  atomic_t read_count;
  atomic_t write_count;

  /* 同步原語 */
  struct mutex lock;

  /* 裝置管理 */
  dev_t devno;
  struct cdev cdev;
  struct class* cls;
  struct device* dev;

  /* procfs */
  struct proc_dir_entry* proc_entry;
} drv;

/* ========================================================
 * Section 1: File Operations (VFS 介面)
 * ======================================================== */

static int chardev_open(struct inode* inode, struct file* filp) {
  atomic_inc(&drv.open_count);
  pr_info("[chardev] open() called, total opens: %d\n",
          atomic_read(&drv.open_count));
  return 0;
}

static int chardev_release(struct inode* inode, struct file* filp) {
  pr_info("[chardev] release() called\n");
  return 0;
}

static ssize_t chardev_read(struct file* filp, char __user* ubuf, size_t count,
                            loff_t* ppos) {
  int to_copy, not_copied;

  if (*ppos >= drv.buf_len) return 0; /* EOF */

  mutex_lock(&drv.lock);

  to_copy = min((size_t)(drv.buf_len - *ppos), count);
  not_copied = copy_to_user(ubuf, drv.buf + *ppos, to_copy);

  *ppos += (to_copy - not_copied);
  atomic_inc(&drv.read_count);

  mutex_unlock(&drv.lock);

  pr_info("[chardev] read() %d bytes\n", to_copy - not_copied);
  return to_copy - not_copied;
}

static ssize_t chardev_write(struct file* filp, const char __user* ubuf,
                             size_t count, loff_t* ppos) {
  int not_copied;

  if (drv.read_only) {
    pr_warn("[chardev] write() blocked: read-only mode\n");
    // return -EPERM;
    return -EACCES;
  }

  if (count > BUF_SIZE) count = BUF_SIZE;

  mutex_lock(&drv.lock);

  not_copied = copy_from_user(drv.buf, ubuf, count);
  drv.buf_len = count - not_copied;
  *ppos = drv.buf_len;
  atomic_inc(&drv.write_count);

  mutex_unlock(&drv.lock);

  pr_info("[chardev] write() %zu bytes\n", count - not_copied);
  return count - not_copied;
}

static long chardev_ioctl(struct file* filp, unsigned int cmd,
                          unsigned long arg) {
  int val, ret = 0;

  /* 基本合法性驗證 */
  if (_IOC_TYPE(cmd) != CHARDEV_MAGIC) return -ENOTTY;
  if (_IOC_NR(cmd) > CHARDEV_MAGIC_MAX) return -ENOTTY;

  switch (cmd) {
    case IOCTL_RESET_BUF:
      mutex_lock(&drv.lock);
      memset(drv.buf, 0, BUF_SIZE);
      drv.buf_len = 0;
      mutex_unlock(&drv.lock);
      pr_info("[chardev] ioctl: buffer reset\n");
      break;

    case IOCTL_GET_LEN:
      val = drv.buf_len;
      if (copy_to_user((int __user*)arg, &val, sizeof(val))) ret = -EFAULT;
      pr_info("[chardev] ioctl: get_len = %d\n", val);
      break;

    case IOCTL_SET_RDONLY:
      if (copy_from_user(&val, (int __user*)arg, sizeof(val))) {
        ret = -EFAULT;
        break;
      }
      drv.read_only = !!val;
      pr_info("[chardev] ioctl: set_rdonly = %d\n", drv.read_only);
      break;

    default:
      ret = -ENOTTY;
  }

  return ret;
}

static const struct file_operations chardev_fops = {
    .owner = THIS_MODULE,
    .open = chardev_open,
    .release = chardev_release,
    .read = chardev_read,
    .write = chardev_write,
    .unlocked_ioctl = chardev_ioctl,
};

/* ========================================================
 * Section 2: procfs  (/proc/chardev_info)
 * ======================================================== */

static int proc_show(struct seq_file* m, void* v) {
  seq_printf(m, "=== chardev driver status ===\n");
  seq_printf(m, "buf_len    : %d\n", drv.buf_len);
  seq_printf(m, "read_only  : %d\n", drv.read_only);
  seq_printf(m, "open_count : %d\n", atomic_read(&drv.open_count));
  seq_printf(m, "read_count : %d\n", atomic_read(&drv.read_count));
  seq_printf(m, "write_count: %d\n", atomic_read(&drv.write_count));
  seq_printf(m, "buf_content: %.*s\n", drv.buf_len, drv.buf);
  return 0;
}

/* single_open 自動處理 seq_file 的 open/release */
static int proc_open(struct inode* inode, struct file* file) {
  return single_open(file, proc_show, NULL);
}

static const struct proc_ops chardev_proc_ops = {
    .proc_open = proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* ========================================================
 * Section 3: sysfs attributes
 * (/sys/class/chardev/chardev0/<attr>)
 * ======================================================== */

/* --- buf_len (唯讀) --- */
static ssize_t buf_len_show(struct device* dev, struct device_attribute* attr,
                            char* buf) {
  return sysfs_emit(buf, "%d\n", drv.buf_len);
}
static DEVICE_ATTR_RO(buf_len);

/* --- read_only (讀寫) --- */
static ssize_t read_only_show(struct device* dev, struct device_attribute* attr,
                              char* buf) {
  return sysfs_emit(buf, "%d\n", drv.read_only);
}

static ssize_t read_only_store(struct device* dev,
                               struct device_attribute* attr, const char* buf,
                               size_t count) {
  int val;
  if (kstrtoint(buf, 10, &val)) return -EINVAL;
  drv.read_only = !!val;
  return count;
}
static DEVICE_ATTR_RW(read_only);

/* --- stats (唯讀，一次顯示所有計數器) --- */
static ssize_t stats_show(struct device* dev, struct device_attribute* attr,
                          char* buf) {
  return sysfs_emit(buf, "open=%d read=%d write=%d\n",
                    atomic_read(&drv.open_count), atomic_read(&drv.read_count),
                    atomic_read(&drv.write_count));
}
static DEVICE_ATTR_RO(stats);

/* 把所有 attribute 打包成 group，方便一次 create/remove */
static struct attribute* chardev_attrs[] = {
    &dev_attr_buf_len.attr,
    &dev_attr_read_only.attr,
    &dev_attr_stats.attr,
    NULL,
};
ATTRIBUTE_GROUPS(chardev);

/* ========================================================
 * Section 4: Module init / exit
 * ======================================================== */

static int __init chardev_init(void) {
  int ret;

  /* 1. 動態配置 kernel buffer */
  drv.buf = kzalloc(BUF_SIZE, GFP_KERNEL);
  if (!drv.buf) return -ENOMEM;

  /* 2. 初始化 mutex、atomic 計數器 */
  mutex_init(&drv.lock);
  atomic_set(&drv.open_count, 0);
  atomic_set(&drv.read_count, 0);
  atomic_set(&drv.write_count, 0);

  /* 3. 動態申請 major/minor number */
  ret = alloc_chrdev_region(&drv.devno, 0, 1, DRIVER_NAME);
  if (ret < 0) {
    pr_err("[chardev] alloc_chrdev_region failed: %d\n", ret);
    goto err_buf;
  }
  pr_info("[chardev] major=%d minor=%d\n", MAJOR(drv.devno), MINOR(drv.devno));

  /* 4. 初始化並加入 cdev */
  cdev_init(&drv.cdev, &chardev_fops);
  drv.cdev.owner = THIS_MODULE;
  ret = cdev_add(&drv.cdev, drv.devno, 1);
  if (ret) {
    pr_err("[chardev] cdev_add failed: %d\n", ret);
    goto err_region;
  }

  /* 5. 建立 /sys/class/chardev，帶 sysfs attribute group */
  // kernel（<= 5.x / early 6.x）
  // drv.cls = class_create(THIS_MODULE, CLASS_NAME);
  drv.cls = class_create(CLASS_NAME);
  if (IS_ERR(drv.cls)) {
    ret = PTR_ERR(drv.cls);
    goto err_cdev;
  }
  /* 把 attribute group 掛上去 */
  drv.cls->dev_groups = chardev_groups;

  /* 6. 建立 /sys/class/chardev/chardev0 + /dev/chardev0 (udev) */
  drv.dev = device_create(drv.cls, NULL, drv.devno, NULL, "chardev0");
  if (IS_ERR(drv.dev)) {
    ret = PTR_ERR(drv.dev);
    goto err_class;
  }

  /* 7. 建立 /proc/chardev_info */
  drv.proc_entry = proc_create(PROC_ENTRY_NAME, 0444, NULL, &chardev_proc_ops);
  if (!drv.proc_entry) {
    ret = -ENOMEM;
    goto err_device;
  }

  pr_info("[chardev] driver loaded successfully\n");
  return 0;

/* ===== 錯誤回滾路徑（反向清理） ===== */
err_device:
  device_destroy(drv.cls, drv.devno);
err_class:
  class_destroy(drv.cls);
err_cdev:
  cdev_del(&drv.cdev);
err_region:
  unregister_chrdev_region(drv.devno, 1);
err_buf:
  kfree(drv.buf);
  return ret;
}

static void __exit chardev_exit(void) {
  proc_remove(drv.proc_entry);
  device_destroy(drv.cls, drv.devno);
  class_destroy(drv.cls);
  cdev_del(&drv.cdev);
  unregister_chrdev_region(drv.devno, 1);
  kfree(drv.buf);
  pr_info("[chardev] driver unloaded\n");
}

module_init(chardev_init);
module_exit(chardev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Custom chardev with sysfs and procfs");
MODULE_VERSION("1.0");
