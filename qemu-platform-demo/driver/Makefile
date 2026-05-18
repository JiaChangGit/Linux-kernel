# Out-of-tree kernel module Makefile
# Usage: make KDIR=<kernel-source> ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

DRIVER_NAME := myled_ctrl
obj-m       := $(DRIVER_NAME).o

# ── Kernel source directory ─────────────────────────────────────────
KDIR ?= /lib/modules/$(shell uname -r)/build

# ── Cross-compilation defaults ──────────────────────────────────────
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

# ── Build artefacts dir ─────────────────────────────────────────────
BUILD_DIR := $(PWD)/build

.PHONY: all clean modules_install

all:
	@mkdir -p $(BUILD_DIR)
	$(MAKE) -C $(KDIR)                      \
	        M=$(PWD)                         \
	        ARCH=$(ARCH)                     \
	        CROSS_COMPILE=$(CROSS_COMPILE)   \
	        modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf $(BUILD_DIR) Module.symvers modules.order

# Copy .ko into rootfs overlay for packaging
install:
	cp $(DRIVER_NAME).ko ../rootfs/overlay/
