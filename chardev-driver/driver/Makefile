# 取得 kernel build 目錄

KDIR := /lib/modules/$(shell uname -r)/build



# 要編譯的 module

obj-m += chardev.o



# 預設目標

all:

	$(MAKE) -C $(KDIR) M=$(PWD) modules



clean:

	$(MAKE) -C $(KDIR) M=$(PWD) clean



# 方便直接 make install / uninstall

install: all

	sudo insmod chardev.ko



uninstall:

	sudo rmmod chardev || true
