/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MYLED_CTRL_H
#define _MYLED_CTRL_H

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* Register offsets：DTS 宣告一段 MMIO，driver 在這段範圍內定義暫存器。 */
#define MYLED_REG_CTRL 0x00
#define MYLED_REG_BRIGHTNESS 0x04
#define MYLED_REG_COLOR 0x08
#define MYLED_REG_STATUS 0x0C
#define MYLED_REG_VERSION 0x10

/* CTRL bits：sysfs enable/blink 會修改這些 bit。 */
#define MYLED_CTRL_ENABLE BIT(0)
#define MYLED_CTRL_BLINK BIT(1)
#define MYLED_CTRL_PWM_AUTO BIT(2)

/* STATUS bits：目前只回報 ready/fault。 */
#define MYLED_STATUS_READY BIT(0)
#define MYLED_STATUS_FAULT BIT(1)

/* 固定 MMIO 位置：必須和 myled-controller@0d000000 的 reg 完全一致。 */
#define MYLED_MMIO_BASE 0x0d000000ULL
#define MYLED_MMIO_SIZE 0x1000U

/* Driver-side limits：先在 sysfs 邊界擋掉不合法輸入。 */
#define MYLED_MAX_BRIGHTNESS 255U
#define MYLED_HW_VERSION 0xAB01U
#define MYLED_REG_SIZE 0x14U
#define MYLED_SIM_REG_COUNT 8

/**
 * struct myled_priv - 每個 platform device 專用的狀態資料
 * @base:       真 MMIO 模式下 ioremap 後的 base；simulated mode 不使用
 * @mmio_size:  DT reg 轉成 resource 後的大小，用來做越界檢查
 * @dev:        對應的 Linux device，供 log、OF property 與 drvdata 使用
 * @num_leds:   DT num-leds 屬性；缺少時由 probe() 設為 1
 * @simulated:  使用 shadow register bank，避免碰 QEMU 中不存在的硬體
 * @sim_regs:   simulated mode 的 32-bit 暫存器陣列
 * @lock:       保護 register read/write/RMW，避免 sysfs 與 PM callback 交錯
 */
struct myled_priv {
  void __iomem* base;
  resource_size_t mmio_size;
  struct device* dev;
  u32 num_leds;
  bool simulated;
  u32 sim_regs[MYLED_SIM_REG_COUNT];
  spinlock_t lock;
};

#endif /* _MYLED_CTRL_H */
