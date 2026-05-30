/**
 * chardev.c - Linux character device driver 範例
 *
 * 對外介面：
 *  - /dev/chardev0：VFS file operations，支援 open/read/write/ioctl/release。
 *  - /proc/chardev_info：procfs 診斷資訊，方便用 cat 查看目前狀態。
 *  - /sys/class/chardev/chardev0/：sysfs device attributes。
 *
 * 主要示範：
 *  - copy_to_user()/copy_from_user()：安全跨越 user space 與 kernel space。
 *  - mutex：保護共享 buffer，避免並行讀寫造成資料不一致。
 *  - atomic_t：記錄 open/read/write 次數。
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
#include <linux/version.h>

/* ===== 常數定義 (constants) ===== */
#define DRIVER_NAME "chardev"
#define CLASS_NAME "chardev"
#define BUF_SIZE 4096
#define PROC_ENTRY_NAME "chardev_info"

/* ===== 驅動全域狀態 (module-global driver state) ===== */
static struct {
  /* 核心緩衝區 (kernel buffer)：所有 fd 共用同一份資料。 */
  char* buf;
  int buf_len;
  int read_only;

  /* 統計計數器 (atomic counters)：只做單純遞增，不保護其他狀態。 */
  atomic_t open_count;
  atomic_t read_count;
  atomic_t write_count;

  /* 互斥鎖 (mutex)：保護 buffer copy/reset 與 buf_len 更新。 */
  struct mutex lock;

  /* 裝置管理 (device model / cdev registration)。 */
  dev_t devno;
  struct cdev cdev;
  struct class* cls;
  struct device* dev;

  /* procfs entry handle，卸載時交給 proc_remove()。 */
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

  if (*ppos >= drv.buf_len) return 0; /* EOF：目前 fd 已讀到有效資料尾端。 */

  mutex_lock(&drv.lock);

  /* read() 只能回傳目前剩餘資料；不可讀超過 drv.buf_len。 */
  to_copy = min((size_t)(drv.buf_len - *ppos), count);
  not_copied = copy_to_user(ubuf, drv.buf + *ppos, to_copy);

  /* file position 只推進實際成功複製的 bytes。 */
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
    return -EACCES;
  }

  /* 固定大小 buffer：超過 BUF_SIZE 的資料會被截斷。 */
  if (count > BUF_SIZE) count = BUF_SIZE;

  mutex_lock(&drv.lock);

  /* user pointer 不可用 memcpy()，必須用 copy_from_user()。 */
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

  /* 驗證 ioctl command 是否屬於本 driver。 */
  if (_IOC_TYPE(cmd) != CHARDEV_MAGIC) return -ENOTTY;
  if (_IOC_NR(cmd) > CHARDEV_MAGIC_MAX) return -ENOTTY;

  switch (cmd) {
    case IOCTL_RESET_BUF:
      /* reset 會改 buffer 與 buf_len，因此與 read/write 共用同一把 mutex。 */
      mutex_lock(&drv.lock);
      memset(drv.buf, 0, BUF_SIZE);
      drv.buf_len = 0;
      mutex_unlock(&drv.lock);
      pr_info("[chardev] ioctl: buffer reset\n");
      break;

    case IOCTL_GET_LEN:
      /* 將目前資料長度回傳給 userspace。 */
      val = drv.buf_len;
      if (copy_to_user((int __user*)arg, &val, sizeof(val))) ret = -EFAULT;
      pr_info("[chardev] ioctl: get_len = %d\n", val);
      break;

    case IOCTL_SET_RDONLY:
      /* 從 userspace 讀入 int；非 0 一律視為 true。 */
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

/* single_open 適合一次性狀態輸出，後續 read 由 seq_read 處理。 */
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

/* --- buf_len：唯讀屬性，回傳目前有效資料長度。 --- */
static ssize_t buf_len_show(struct device* dev, struct device_attribute* attr,
                            char* buf) {
  return sysfs_emit(buf, "%d\n", drv.buf_len);
}
static DEVICE_ATTR_RO(buf_len);

/* --- read_only：讀寫屬性，0 表示可寫，1 表示拒絕 write()。 --- */
static ssize_t read_only_show(struct device* dev, struct device_attribute* attr,
                              char* buf) {
  return sysfs_emit(buf, "%d\n", drv.read_only);
}

static ssize_t read_only_store(struct device* dev,
                               struct device_attribute* attr, const char* buf,
                               size_t count) {
  int val;
  /* sysfs 輸入是文字，kstrtoint() 會處理換行與格式錯誤。 */
  if (kstrtoint(buf, 10, &val)) return -EINVAL;
  drv.read_only = !!val;
  return count;
}
static DEVICE_ATTR_RW(read_only);

/* --- stats：唯讀屬性，一次顯示 open/read/write 計數。 --- */
static ssize_t stats_show(struct device* dev, struct device_attribute* attr,
                          char* buf) {
  return sysfs_emit(buf, "open=%d read=%d write=%d\n",
                    atomic_read(&drv.open_count), atomic_read(&drv.read_count),
                    atomic_read(&drv.write_count));
}
static DEVICE_ATTR_RO(stats);

/* 把所有 sysfs attributes 打包成 group，讓 device_create() 自動建立。 */
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

  /* 1. 配置 kernel buffer；kzalloc 會清零，避免讀到未初始化資料。 */
  drv.buf = kzalloc(BUF_SIZE, GFP_KERNEL);
  if (!drv.buf) return -ENOMEM;

  /* 2. 初始化同步工具與統計計數器。 */
  mutex_init(&drv.lock);
  atomic_set(&drv.open_count, 0);
  atomic_set(&drv.read_count, 0);
  atomic_set(&drv.write_count, 0);

  /* 3. 動態申請 major/minor number，避免手動指定造成衝突。 */
  ret = alloc_chrdev_region(&drv.devno, 0, 1, DRIVER_NAME);
  if (ret < 0) {
    pr_err("[chardev] alloc_chrdev_region failed: %d\n", ret);
    goto err_buf;
  }
  pr_info("[chardev] major=%d minor=%d\n", MAJOR(drv.devno), MINOR(drv.devno));

  /* 4. 將 file_operations 綁到 cdev，讓 VFS 能 dispatch 到本 driver。 */
  cdev_init(&drv.cdev, &chardev_fops);
  drv.cdev.owner = THIS_MODULE;
  ret = cdev_add(&drv.cdev, drv.devno, 1);
  if (ret) {
    pr_err("[chardev] cdev_add failed: %d\n", ret);
    goto err_region;
  }

  /* 5. 建立 /sys/class/chardev。class_create() 參數依 kernel 版本不同。 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
  drv.cls = class_create(CLASS_NAME);
#else
  drv.cls = class_create(THIS_MODULE, CLASS_NAME);
#endif
  if (IS_ERR(drv.cls)) {
    ret = PTR_ERR(drv.cls);
    goto err_cdev;
  }

  /* 將 sysfs attribute group 掛到 class，device_create() 時會自動套用。 */
  drv.cls->dev_groups = chardev_groups;

  /* 6. 建立 /sys/class/chardev/chardev0，udev 通常會同步建立 /dev/chardev0。 */
  drv.dev = device_create(drv.cls, NULL, drv.devno, NULL, "chardev0");
  if (IS_ERR(drv.dev)) {
    ret = PTR_ERR(drv.dev);
    goto err_class;
  }

  /* 7. 建立 /proc/chardev_info，提供簡單診斷輸出。 */
  drv.proc_entry = proc_create(PROC_ENTRY_NAME, 0444, NULL, &chardev_proc_ops);
  if (!drv.proc_entry) {
    ret = -ENOMEM;
    goto err_device;
  }

  pr_info("[chardev] driver loaded successfully\n");
  return 0;

/* ===== 錯誤回滾路徑：依資源建立的反向順序清理。 ===== */
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
MODULE_AUTHOR("JIA");
MODULE_DESCRIPTION("Custom chardev with sysfs and procfs");
MODULE_VERSION("1.0");
