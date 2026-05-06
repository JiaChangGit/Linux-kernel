// SPDX-License-Identifier: GPL-2.0
/**
 * myled_ctrl.c - Virtual LED Controller Platform Driver
 *
 * Demonstrates for a firmware resume:
 *   - Platform driver registration & OF (Device Tree) matching
 *   - devm_* resource management (ioremap, kzalloc)
 *   - Simulated MMIO register bank (runs cleanly on QEMU virt)
 *   - sysfs attribute interface (enable, brightness, color, status)
 *   - DT property parsing (num-leds, label, default-brightness)
 *   - Proper probe/remove lifecycle with error unwinding
 *   - PM suspend/resume skeleton
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

/* ================================================================
 *  Low-level register helpers
 * ================================================================ */

static u32 myled_reg_read(struct myled_priv* priv, u32 off) {
  unsigned long flags;
  u32 val;

  spin_lock_irqsave(&priv->lock, flags);
  if (priv->simulated)
    val = priv->sim_regs[off / 4];
  else
    val = readl(priv->base + off);
  spin_unlock_irqrestore(&priv->lock, flags);

  return val;
}

static void myled_reg_write(struct myled_priv* priv, u32 off, u32 val) {
  unsigned long flags;

  spin_lock_irqsave(&priv->lock, flags);
  if (priv->simulated)
    priv->sim_regs[off / 4] = val;
  else
    writel(val, priv->base + off);
  spin_unlock_irqrestore(&priv->lock, flags);
}

static void myled_reg_set_bits(struct myled_priv* priv, u32 off, u32 mask) {
  u32 val = myled_reg_read(priv, off);
  myled_reg_write(priv, off, val | mask);
}

static void myled_reg_clr_bits(struct myled_priv* priv, u32 off, u32 mask) {
  u32 val = myled_reg_read(priv, off);
  myled_reg_write(priv, off, val & ~mask);
}

/* ================================================================
 *  Hardware init / shutdown
 * ================================================================ */

// static int myled_hw_init(struct myled_priv* priv) {
//   u32 ver;

//   /* In simulated mode, seed the VERSION and STATUS read-only regs */
//   if (priv->simulated) {
//     priv->sim_regs[MYLED_REG_VERSION / 4] = MYLED_HW_VERSION;
//     priv->sim_regs[MYLED_REG_STATUS / 4] = MYLED_STATUS_READY;
//   }

//   ver = myled_reg_read(priv, MYLED_REG_VERSION);
//   if (ver != MYLED_HW_VERSION) {
//     dev_warn(priv->dev, "unexpected HW version 0x%04x (expected 0x%04x)\n",
//     ver,
//              MYLED_HW_VERSION);
//     /* non-fatal: continue for demo purposes */
//   }

//   /* Apply default brightness from DT */
//   u32 brightness;
//   if (of_property_read_u32(priv->dev->of_node, "default-brightness",
//                            &brightness))
//     brightness = 128; /* fallback default */
//   brightness = min(brightness, (u32)MYLED_MAX_BRIGHTNESS);
//   myled_reg_write(priv, MYLED_REG_BRIGHTNESS, brightness);

//   /* Enable the controller */
//   myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);

//   dev_info(priv->dev, "HW init OK (ver=0x%04x, brightness=%u, leds=%u)\n",
//   ver,
//            brightness, priv->num_leds);
//   return 0;
// }

static int myled_hw_init(struct myled_priv* priv) {
  u32 ver;

  ver = myled_reg_read(priv, MYLED_REG_VERSION);

  /* 0xffffffff 代表 MMIO 無回應（地址衝突或無實體裝置）
   * 自動 fallback 到 simulated mode                    */
  if (ver != MYLED_HW_VERSION) {
    dev_warn(priv->dev,
             "no HW response (got 0x%08x), switching to simulated mode\n", ver);
    priv->simulated = true;
    priv->sim_regs[MYLED_REG_VERSION / 4] = MYLED_HW_VERSION;
    priv->sim_regs[MYLED_REG_STATUS / 4] = MYLED_STATUS_READY;
  }

  /* 之後的讀寫都走 sim_regs，結果正確 */
  u32 brightness;
  if (of_property_read_u32(priv->dev->of_node, "default-brightness",
                           &brightness))
    brightness = 128;
  brightness = min(brightness, (u32)MYLED_MAX_BRIGHTNESS);
  myled_reg_write(priv, MYLED_REG_BRIGHTNESS, brightness);
  myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);

  dev_info(priv->dev,
           "HW init OK (ver=0x%04x, brightness=%u, leds=%u, simulated=%s)\n",
           myled_reg_read(priv, MYLED_REG_VERSION), brightness, priv->num_leds,
           priv->simulated ? "yes" : "no");
  return 0;
}

static void myled_hw_shutdown(struct myled_priv* priv) {
  myled_reg_clr_bits(
      priv, MYLED_REG_CTRL,
      MYLED_CTRL_ENABLE | MYLED_CTRL_BLINK | MYLED_CTRL_PWM_AUTO);
  myled_reg_write(priv, MYLED_REG_BRIGHTNESS, 0);
  dev_info(priv->dev, "HW shutdown complete\n");
}

/* ================================================================
 *  sysfs attributes
 * ================================================================ */

/* ── enable ── */
static ssize_t enable_show(struct device* dev, struct device_attribute* attr,
                           char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val = myled_reg_read(priv, MYLED_REG_CTRL);
  return sysfs_emit(buf, "%u\n", !!(val & MYLED_CTRL_ENABLE));
}

static ssize_t enable_store(struct device* dev, struct device_attribute* attr,
                            const char* buf, size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  bool enable;
  int ret;

  ret = kstrtobool(buf, &enable);
  if (ret) return ret;

  if (enable)
    myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
  else
    myled_reg_clr_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);

  dev_dbg(dev, "LED %s\n", enable ? "enabled" : "disabled");
  return count;
}
static DEVICE_ATTR_RW(enable);

/* ── brightness ── */
static ssize_t brightness_show(struct device* dev,
                               struct device_attribute* attr, char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  return sysfs_emit(buf, "%u\n", myled_reg_read(priv, MYLED_REG_BRIGHTNESS));
}

static ssize_t brightness_store(struct device* dev,
                                struct device_attribute* attr, const char* buf,
                                size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = kstrtou32(buf, 10, &val);
  if (ret) return ret;

  if (val > MYLED_MAX_BRIGHTNESS) return -EINVAL;

  myled_reg_write(priv, MYLED_REG_BRIGHTNESS, val);
  dev_dbg(dev, "brightness set to %u\n", val);
  return count;
}
static DEVICE_ATTR_RW(brightness);

/* ── color (hex RRGGBB) ── */
static ssize_t color_show(struct device* dev, struct device_attribute* attr,
                          char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val = myled_reg_read(priv, MYLED_REG_COLOR);
  return sysfs_emit(buf, "%06x\n", val & 0xFFFFFF);
}

static ssize_t color_store(struct device* dev, struct device_attribute* attr,
                           const char* buf, size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val;
  int ret;

  ret = kstrtou32(buf, 16, &val);
  if (ret) return ret;

  myled_reg_write(priv, MYLED_REG_COLOR, val & 0xFFFFFF);
  dev_dbg(dev, "color set to #%06x\n", val & 0xFFFFFF);
  return count;
}
static DEVICE_ATTR_RW(color);

/* ── blink ── */
static ssize_t blink_show(struct device* dev, struct device_attribute* attr,
                          char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 val = myled_reg_read(priv, MYLED_REG_CTRL);
  return sysfs_emit(buf, "%u\n", !!(val & MYLED_CTRL_BLINK));
}

static ssize_t blink_store(struct device* dev, struct device_attribute* attr,
                           const char* buf, size_t count) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  bool blink;
  int ret;

  ret = kstrtobool(buf, &blink);
  if (ret) return ret;

  if (blink)
    myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_BLINK);
  else
    myled_reg_clr_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_BLINK);

  return count;
}
static DEVICE_ATTR_RW(blink);

/* ── status (read-only) ── */
static ssize_t status_show(struct device* dev, struct device_attribute* attr,
                           char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 s = myled_reg_read(priv, MYLED_REG_STATUS);
  return sysfs_emit(buf, "ready=%u fault=%u\n", !!(s & MYLED_STATUS_READY),
                    !!(s & MYLED_STATUS_FAULT));
}
static DEVICE_ATTR_RO(status);

/* ── info (read-only, device summary) ── */
static ssize_t info_show(struct device* dev, struct device_attribute* attr,
                         char* buf) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  u32 ver = myled_reg_read(priv, MYLED_REG_VERSION);
  u32 ctrl = myled_reg_read(priv, MYLED_REG_CTRL);
  u32 brg = myled_reg_read(priv, MYLED_REG_BRIGHTNESS);
  u32 col = myled_reg_read(priv, MYLED_REG_COLOR);

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

/* ================================================================
 *  Probe / Remove
 * ================================================================ */

static int myled_probe(struct platform_device* pdev) {
  struct device* dev = &pdev->dev;
  struct myled_priv* priv;
  struct resource* res;
  int ret;

  dev_info(dev, "probe() called — compatible matched via Device Tree\n");

  /* ── Allocate private data (devm: auto-freed on device removal) ── */
  priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
  if (!priv) return -ENOMEM;

  priv->dev = dev;
  spin_lock_init(&priv->lock);

  /* ── Parse Device Tree properties ── */
  ret = of_property_read_u32(dev->of_node, "num-leds", &priv->num_leds);
  if (ret) {
    dev_warn(dev, "num-leds not found in DT, defaulting to 1\n");
    priv->num_leds = 1;
  }

  {
    const char* label;
    if (!of_property_read_string(dev->of_node, "label", &label))
      dev_info(dev, "label = \"%s\"\n", label);
  }

  /* ── MMIO resource mapping ── */
  res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (!res) {
    dev_warn(dev, "no MEM resource in DT — entering simulated mode\n");
    priv->simulated = true;
  } else {
    dev_info(dev, "MMIO region: [0x%08llx - 0x%08llx]\n", (u64)res->start,
             (u64)res->end);

    priv->base = devm_ioremap_resource(dev, res);
    if (IS_ERR(priv->base)) {
      dev_warn(dev, "ioremap failed (%ld) — entering simulated mode\n",
               PTR_ERR(priv->base));
      priv->base = NULL;
      priv->simulated = true;
    }
  }

  if (priv->simulated)
    dev_info(dev, "** SIMULATED mode: register accesses use shadow array **\n");

  /* ── Store driver data before sysfs (callbacks need it) ── */
  platform_set_drvdata(pdev, priv);
  dev_set_drvdata(dev, priv);

  /* ── Hardware initialisation ── */
  ret = myled_hw_init(priv);
  if (ret) {
    dev_err(dev, "HW init failed: %d\n", ret);
    return ret;
  }

  /* ── Create sysfs attribute group ── */
  ret = sysfs_create_group(&dev->kobj, &myled_attr_group);
  if (ret) {
    dev_err(dev, "sysfs group creation failed: %d\n", ret);
    myled_hw_shutdown(priv);
    return ret;
  }

  /* ── PM runtime enable ── */
  pm_runtime_enable(dev);

  dev_info(dev,
           "probe() succeeded — sysfs at /sys/bus/platform/devices/%s/myled/\n",
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

/* ================================================================
 *  Power Management
 * ================================================================ */

static int __maybe_unused myled_suspend(struct device* dev) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  dev_info(dev, "suspend: disabling controller\n");
  myled_reg_clr_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
  return 0;
}

static int __maybe_unused myled_resume(struct device* dev) {
  struct myled_priv* priv = dev_get_drvdata(dev);
  dev_info(dev, "resume: re-enabling controller\n");
  myled_reg_set_bits(priv, MYLED_REG_CTRL, MYLED_CTRL_ENABLE);
  return 0;
}

static const struct dev_pm_ops myled_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(myled_suspend, myled_resume)};

/* ================================================================
 *  OF (Device Tree) Match Table
 * ================================================================ */

static const struct of_device_id myled_of_match[] = {
    {.compatible = "myvendor,myled-v1"},
    {.compatible = "myvendor,myled"},
    {/* sentinel */}};
MODULE_DEVICE_TABLE(of, myled_of_match);

/* ================================================================
 *  Platform Driver Registration
 * ================================================================ */

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

MODULE_AUTHOR("Your Name <you@example.com>");
MODULE_DESCRIPTION("Virtual LED Controller Platform Driver (QEMU Demo)");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");
