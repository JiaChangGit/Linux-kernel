# Out-of-tree module build。
# 範例：make KDIR=<kernel-source> ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

DRIVER_NAME := myled_ctrl
obj-m       := $(DRIVER_NAME).o

# Kernel source/build 目錄；scripts/03_build_driver.sh 會傳入專案內的 linux-6.6.30。
KDIR ?= /lib/modules/$(shell uname -r)/build

# ARM64 cross-build 預設值；命令列可覆寫。
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

# 保留 build/ 目錄給後續擴充；目前 Kbuild 產物仍放在 driver/。
BUILD_DIR := $(PWD)/build

.PHONY: all clean install

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

# 供 scripts/04_build_rootfs.sh 打包進 initramfs。
install:
	cp $(DRIVER_NAME).ko ../rootfs/overlay/
