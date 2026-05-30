# 目前執行中 kernel 的 build directory。
KDIR := /lib/modules/$(shell uname -r)/build

# 外部模組原始碼目錄。
# 使用 CURDIR 而不是 PWD，避免 `make -C driver` 時 M= 指到呼叫端目錄。
MODULE_DIR := $(CURDIR)

# 要建置的 kernel module；kernel build system 會產生 chardev.ko。
obj-m += chardev.o

.PHONY: all clean install uninstall

all:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(MODULE_DIR) clean

# 方便手動載入 / 卸載。完整驗證流程建議使用 scripts/load.sh。
install: all
	sudo insmod chardev.ko

uninstall:
	sudo rmmod chardev || true
