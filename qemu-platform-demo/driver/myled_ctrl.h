/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MYLED_CTRL_H
#define _MYLED_CTRL_H

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* ── Register Offsets ─────────────────────────────── */
#define MYLED_REG_CTRL 0x00
#define MYLED_REG_BRIGHTNESS 0x04
#define MYLED_REG_COLOR 0x08
#define MYLED_REG_STATUS 0x0C
#define MYLED_REG_VERSION 0x10

/* ── CTRL Register Bit Fields ─────────────────────── */
#define MYLED_CTRL_ENABLE BIT(0)
#define MYLED_CTRL_BLINK BIT(1)
#define MYLED_CTRL_PWM_AUTO BIT(2)

/* ── STATUS Register Bit Fields ───────────────────── */
#define MYLED_STATUS_READY BIT(0)
#define MYLED_STATUS_FAULT BIT(1)

/* ── Constraints ──────────────────────────────────── */
#define MYLED_MAX_BRIGHTNESS 255U
#define MYLED_HW_VERSION 0xAB01U
#define MYLED_SIM_REG_COUNT 8

/**
 * struct myled_priv - per-device private data
 * @base:       ioremap'd register base
 * @dev:        back-pointer to struct device
 * @num_leds:   number of LEDs (from DT "num-leds" property)
 * @simulated:  true when no real MMIO HW (QEMU soft demo mode)
 * @sim_regs:   shadow register bank used in simulated mode
 * @lock:       protects register access
 */
struct myled_priv {
  void __iomem* base;
  struct device* dev;
  u32 num_leds;
  bool simulated;
  u32 sim_regs[MYLED_SIM_REG_COUNT];
  spinlock_t lock;
};

#endif /* _MYLED_CTRL_H */
