// SPDX-License-Identifier: GPL-2.0
/*
 * myled_ctrl.c - QEMU virt 上的虛擬 LED platform driver
 *
 * 這支 driver 用來示範一個完整 platform device 流程：
 *   - Device Tree 用 compatible 配對 driver。
 *   - probe() 取得並驗證 MMIO resource。
 *   - QEMU 沒有真硬體時，改用 shadow register 模擬暫存器。
 *   - sysfs 提供簡單、可測試的使用者空間介面。
 */

#include "myled_ctrl.h"

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/version.h>

/* Register access helpers：集中處理 offset 檢查、simulated mode 與 locking。 */

static void myled_warn_bad_reg(struct myled_priv* priv, const char* op, u32 off,
                               const char* reason) {
  if (priv && priv->dev)
    dev_warn_ratelimited(priv->dev,
                         "rejecting %s register access at offset 0x%x: %s\n",
                         op, off, reason);
}

static int myled_validate_reg_access(struct myled_priv* priv, u32 off,
                                     const char* op) {
  if (!priv)
    return -ENODEV;

  if (off & (sizeof(u32) - 1)) {
    myled_warn_bad_reg(priv, op, off, "unaligned offset");
    return -EINVAL;
  }

  if (off > MYLED_REG_SIZE - sizeof(u32)) {
    myled_warn_bad_reg(priv, op, off, "offset outside register block");
    return -EINVAL;
  }

  /* simulated mode 只允許落在 shadow register bank 內的 32-bit offset。 */
  if (priv->simulated) {
    if (off / sizeof(u32) >= MYLED_SIM_REG_COUNT) {
      myled_warn_bad_reg(priv, op, off, "offset outside simulated bank");
      return -EINVAL;
    }
    return 0;
  }

  if (!priv->base) {
    myled_warn_bad_reg(priv, op, off, "MMIO base is not mapped");
    return -ENODEV;
  }

  if (priv->mmio_size < sizeof(u32) ||
      (resource_size_t)off > priv->mmio_size - sizeof(u32)) {
    myled_warn_bad_reg(priv, op, off, "offset outside mapped MMIO resource");
    return -ERANGE;
  }

  return 0;
}

static int myled_reg_read(struct myled_priv* priv, u32 off, u32* val) {
  unsigned long flags;
  int ret;

  if (!val)
    return -EINVAL;

  *val = 0;
  ret = myled_validate_reg_access(priv, off, "read");
  if (ret)
    return ret;

  spin_lock_irqsave(&priv->lock, flags);
  if (priv->simulated)
    *val = priv->sim_regs[off / sizeof(u32)];
  else
    *val = readl(priv->base + off);
  spin_unlock_irqrestore(&priv->lock, flags);

  return 0;
}

static int myled_reg_write(struct myled_priv* priv, u32 off, u32 val) {
  unsigned long flags;
  int ret;

  ret = myled_validate_reg_access(priv, off, "write");
  if (ret)
    return ret;

  spin_lock_irqsave(&priv->lock, flags);
  if (priv->simulated)
    priv->sim_regs[off / sizeof(u32)] = val;
  else
    writel(val, priv->base + off);
  spin_unlock_irqrestore(&priv->lock, flags);

  return 0;
}

static int myled_reg_update_bits(struct myled_priv* priv, u32 off, u32 mask,
                                 bool set) {
  unsigned long flags;
  u32 val;
  int ret;

  ret = myled_validate_reg_access(priv, off, "update");
  if (ret)
    return ret;

  spin_lock_irqsave(&priv->lock, flags);
  if (priv->simulated)
    val = priv->sim_regs[off / sizeof(u32)];
  else
    val = readl(priv->base + off);

  if (set)
    val |= mask;
  else
    val &= ~mask;

  if (priv->simulated)
    priv->sim_regs[off / sizeof(u32)] = val;
  else
    writel(val, priv->base + off);
  spin_unlock_irqrestore(&priv->lock, flags);

  return 0;
}

static int myled_reg_set_bits(struct myled_priv* priv, u32 off, u32 mask) {
  return myled_reg_update_bits(priv, off, mask, true);
}

static int myled_reg_clr_bits(struct myled_priv* priv, u32 off, u32 mask) {
  return myled_reg_update_bits(priv, off, mask, false);
}

/* Hardware state：建立初始暫存器狀態，並在 remove/error path 收尾。 */

static void myled_seed_sim_regs(struct myled_priv* priv) {
  unsigned int i;

  for (i = 0; i < MYLED_SIM_REG_COUNT; i++)
    priv->sim_regs[i] = 0;

  priv->sim_regs[MYLED_REG_VERSION / sizeof(u32)] = MYLED_HW_VERSION;
  priv->sim_regs[MYLED_REG_STATUS / sizeof(u32)] = MYLED_STATUS_READY;
}

static int myled_hw_init(struct myled_priv* priv) {
  u32 ver = 0;
  u32 brightness;
  int ret;

  if (priv->simulated)
    myled_seed_sim_regs(priv);

  ret = myled_reg_read(priv, MYLED_REG_VERSION, &ver);
  if (ret || ver != MYLED_HW_VERSION) {
    if (priv->simulated)
      return ret ? ret : -ENODEV;

    dev_warn(priv->dev,
             "HW version unavailable or mismatched (ret=%d, ver=0x%08x); "
             "using simulated registers\n",
             ret, ver);
    priv->simulated = true;
    myled_seed_sim_regs(priv);

    ret = myled_reg_read(priv, MYLED_REG_VERSION, &ver);
    if (ret)
      return ret;
  }

  if (!priv->dev->of_node ||
      of_property_read_u32(priv->dev->of_node, "default-brightness",
                           &brightness))
    brightness = 128;
  brightness = min(brightness, (u32)MYLED_MAX_BRIGHTNESS);

  ret = myled_reg_write(priv, MYLED_REG_BRIGHTNESS, brightness);
  if (ret)
    return ret;

  ret = myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
  if (ret)
    return ret;

  ret = myled_reg_read(priv, MYLED_REG_VERSION, &ver);
  if (ret)
    return ret;

  dev_info(priv->dev,
           "HW init OK (ver=0x%04x, brightness=%u, leds=%u, simulated=%s)\n",
           ver, brightness, priv->num_leds, priv->simulated ? "yes" : "no");
  return 0;
}

static void myled_hw_shutdown(struct myled_priv* priv) {
  int ret;

  if (!priv)
    return;

  ret = myled_reg_clr_bits(
      priv, MYLED_REG_CTRL,
      MYLED_CTRL_ENABLE | MYLED_CTRL_BLINK | MYLED_CTRL_PWM_AUTO);
  if (ret)
    dev_warn(priv->dev, "failed to disable controller: %d\n", ret);

  ret = myled_reg_write(priv, MYLED_REG_BRIGHTNESS, 0);
  if (ret)
    dev_warn(priv->dev, "failed to clear brightness: %d\n", ret);

  dev_info(priv->dev, "HW shutdown complete\n");
}

/* sysfs attributes：每個檔案對應一個簡單屬性，方便 shell 測試。 */

/* enable：對應 CTRL.ENABLE。 */
static ssize_t enable_show(struct device* dev, struct device_attribute* attr,
                           char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = myled_reg_read(priv, MYLED_REG_CTRL, &val);
  if (ret)
    return ret;

  return sysfs_emit(buf, "%u\n", !!(val & MYLED_CTRL_ENABLE));
}

static ssize_t enable_store(struct device* dev, struct device_attribute* attr,
                            const char* buf, size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  bool enable;
  int ret;

  ret = kstrtobool(buf, &enable);
  if (ret)
    return ret;

  if (enable)
    ret = myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
  else
    ret = myled_reg_clr_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
  if (ret)
    return ret;

  dev_dbg(dev, "LED %s\n", enable ? "enabled" : "disabled");
  return count;
}
static DEVICE_ATTR_RW(enable);

/* brightness：只接受 0..255，避免寫入超出 controller 規格的值。 */
static ssize_t brightness_show(struct device* dev,
                               struct device_attribute* attr, char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = myled_reg_read(priv, MYLED_REG_BRIGHTNESS, &val);
  if (ret)
    return ret;

  return sysfs_emit(buf, "%u\n", val);
}

static ssize_t brightness_store(struct device* dev,
                                struct device_attribute* attr, const char* buf,
                                size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = kstrtou32(buf, 10, &val);
  if (ret)
    return ret;

  if (val > MYLED_MAX_BRIGHTNESS)
    return -EINVAL;

  ret = myled_reg_write(priv, MYLED_REG_BRIGHTNESS, val);
  if (ret)
    return ret;

  dev_dbg(dev, "brightness set to %u\n", val);
  return count;
}
static DEVICE_ATTR_RW(brightness);

/* color：使用 RRGGBB 十六進位格式，driver 只保留低 24 bits。 */
static ssize_t color_show(struct device* dev, struct device_attribute* attr,
                          char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = myled_reg_read(priv, MYLED_REG_COLOR, &val);
  if (ret)
    return ret;

  return sysfs_emit(buf, "%06x\n", val & 0xFFFFFF);
}

static ssize_t color_store(struct device* dev, struct device_attribute* attr,
                           const char* buf, size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = kstrtou32(buf, 16, &val);
  if (ret)
    return ret;

  ret = myled_reg_write(priv, MYLED_REG_COLOR, val & 0xFFFFFF);
  if (ret)
    return ret;

  dev_dbg(dev, "color set to #%06x\n", val & 0xFFFFFF);
  return count;
}
static DEVICE_ATTR_RW(color);

/* blink：對應 CTRL.BLINK。 */
static ssize_t blink_show(struct device* dev, struct device_attribute* attr,
                          char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = myled_reg_read(priv, MYLED_REG_CTRL, &val);
  if (ret)
    return ret;

  return sysfs_emit(buf, "%u\n", !!(val & MYLED_CTRL_BLINK));
}

static ssize_t blink_store(struct device* dev, struct device_attribute* attr,
                           const char* buf, size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  bool blink;
  int ret;

  ret = kstrtobool(buf, &blink);
  if (ret)
    return ret;

  if (blink)
    ret = myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_BLINK);
  else
    ret = myled_reg_clr_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_BLINK);
  if (ret)
    return ret;

  return count;
}
static DEVICE_ATTR_RW(blink);

/* status：唯讀，回報 READY/FAULT bit。 */
static ssize_t status_show(struct device* dev, struct device_attribute* attr,
                           char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 s;
  int ret;

  ret = myled_reg_read(priv, MYLED_REG_STATUS, &s);
  if (ret)
    return ret;

  return sysfs_emit(buf, "ready=%u fault=%u\n", !!(s & MYLED_STATUS_READY),
                    !!(s & MYLED_STATUS_FAULT));
}
static DEVICE_ATTR_RO(status);

/* info：唯讀摘要，讓測試腳本一次取得主要狀態。 */
static ssize_t info_show(struct device* dev, struct device_attribute* attr,
                         char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 ver;
  u32 ctrl;
  u32 brg;
  u32 col;
  int ret;

  ret = myled_reg_read(priv, MYLED_REG_VERSION, &ver);
  if (ret)
    return ret;
  ret = myled_reg_read(priv, MYLED_REG_CTRL, &ctrl);
  if (ret)
    return ret;
  ret = myled_reg_read(priv, MYLED_REG_BRIGHTNESS, &brg);
  if (ret)
    return ret;
  ret = myled_reg_read(priv, MYLED_REG_COLOR, &col);
  if (ret)
    return ret;

  return sysfs_emit(buf,
                    "version    : 0x%04x\n"
                    "num_leds   : %u\n"
                    "simulated  : %s\n"
                    "ctrl       : 0x%08x  (en=%u blink=%u pwm=%u)\n"
                    "brightness : %u\n"
                    "color      : #%06x\n",
                    ver, priv->num_leds, priv->simulated ? "yes" : "no", ctrl,
                    !!(ctrl & MYLED_CTRL_ENABLE), !!(ctrl & MYLED_CTRL_BLINK),
                    !!(ctrl & MYLED_CTRL_PWM_AUTO), brg, col & 0xFFFFFF);
}
static DEVICE_ATTR_RO(info);

static struct attribute* myled_attrs[] = {
    &dev_attr_enable.attr,
    &dev_attr_brightness.attr,
    &dev_attr_color.attr,
    &dev_attr_blink.attr,
    &dev_attr_status.attr,
    &dev_attr_info.attr,
    NULL,
};

static const struct attribute_group myled_attr_group = {
    .name = "myled",
    .attrs = myled_attrs,
};

/* Probe/remove：負責 resource 驗證、狀態建立與使用者介面清理。 */

static int myled_probe(struct platform_device* pdev) {
  struct device* dev = &pdev->dev;
  struct myled_priv* priv;
  struct resource* res;
  int ret;

  dev_info(dev, "probe() called - compatible matched via Device Tree\n");

  /* sysfs 與 PM callback 都靠 drvdata 找回這份 private state。 */
  priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
  if (!priv)
    return -ENOMEM;

  priv->dev = dev;
  spin_lock_init(&priv->lock);

  /* num-leds 是展示用屬性；缺少時仍可用單顆 LED 的預設值繼續。 */
  ret = dev->of_node
            ? of_property_read_u32(dev->of_node, "num-leds", &priv->num_leds)
            : -ENODEV;
  if (ret) {
    dev_warn(dev, "num-leds not found in DT, defaulting to 1\n");
    priv->num_leds = 1;
  }

  {
    const char* label;
    if (dev->of_node && !of_property_read_string(dev->of_node, "label", &label))
      dev_info(dev, "label = \"%s\"\n", label);
  }

  priv->simulated =
      dev->of_node && of_property_read_bool(dev->of_node, "myvendor,simulated");

  res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (!res) {
    dev_err(dev, "missing MEM resource\n");
    return -ENODEV;
  }

  priv->mmio_size = resource_size(res);
  dev_info(dev, "MMIO region: [0x%08llx - 0x%08llx]\n",
           (u64)res->start, (u64)res->end);

  if ((u64)res->start != MYLED_MMIO_BASE || priv->mmio_size != MYLED_MMIO_SIZE) {
    dev_err(dev,
            "unexpected MMIO resource: base=0x%08llx size=0x%llx "
            "(expected base=0x%08llx size=0x%x)\n",
            (u64)res->start, (unsigned long long)priv->mmio_size,
            MYLED_MMIO_BASE, MYLED_MMIO_SIZE);
    return -EINVAL;
  }

  /* QEMU 沒有 myled 真實 MMIO model 時，必須避開 ioremap 後的硬體存取。 */
  if (priv->simulated) {
    dev_info(dev, "simulated mode requested by Device Tree\n");
  } else {
    priv->base = devm_ioremap_resource(dev, res);
    if (IS_ERR(priv->base)) {
      ret = PTR_ERR(priv->base);
      priv->base = NULL;
      priv->simulated = true;
      dev_warn(dev, "ioremap failed (%d); using simulated registers\n", ret);
    }
  }

  if (priv->simulated)
    dev_info(dev, "** SIMULATED mode: register accesses use shadow array **\n");

  /* 先設定 drvdata，再建立 sysfs，避免 callback 早到時拿不到 priv。 */
  platform_set_drvdata(pdev, priv);
  dev_set_drvdata(dev, priv);

  ret = myled_hw_init(priv);
  if (ret) {
    dev_err(dev, "HW init failed: %d\n", ret);
    return ret;
  }

  ret = sysfs_create_group(&dev->kobj, &myled_attr_group);
  if (ret) {
    dev_err(dev, "sysfs group creation failed: %d\n", ret);
    myled_hw_shutdown(priv);
    return ret;
  }

  pm_runtime_enable(dev);

  dev_info(dev,
           "probe() succeeded - sysfs at /sys/bus/platform/devices/%s/myled/\n",
           dev_name(dev));
  return 0;
}

static int myled_remove(struct platform_device* pdev) {
  struct device* dev = &pdev->dev;
  struct myled_priv* priv = platform_get_drvdata(pdev);

  dev_info(dev, "remove() called\n");

  pm_runtime_disable(dev);
  sysfs_remove_group(&dev->kobj, &myled_attr_group);
  myled_hw_shutdown(priv);

  dev_info(dev, "remove() complete\n");
  return 0;
}

/* Power management：system sleep 時只保存最小 demo 行為。 */

static int __maybe_unused myled_suspend(struct device* dev) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  dev_info(dev, "suspend: disabling controller\n");
  return myled_reg_clr_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
}

static int __maybe_unused myled_resume(struct device* dev) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  dev_info(dev, "resume: re-enabling controller\n");
  return myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
}

static const struct dev_pm_ops myled_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(myled_suspend, myled_resume)};

/* OF match table：compatible 字串必須和 DTS 節點一致。 */

static const struct of_device_id myled_of_match[] = {
    {.compatible = "myvendor,myled-v1"},
    {.compatible = "myvendor,myled"},
    {/* 結尾哨兵，driver core 以空項目判斷表格結束。 */}};
MODULE_DEVICE_TABLE(of, myled_of_match);

/* Platform driver registration：module 載入後由 driver core 呼叫 probe/remove。 */

static struct platform_driver myled_driver = {
    .probe = myled_probe,
    .remove = myled_remove,
    .driver =
        {
            .name = "myled_ctrl",
            .of_match_table = myled_of_match,
            .pm = &myled_pm_ops,
        },
};

module_platform_driver(myled_driver);

MODULE_AUTHOR("JIA");
MODULE_DESCRIPTION("Virtual LED Controller Platform Driver (QEMU Demo)");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");
