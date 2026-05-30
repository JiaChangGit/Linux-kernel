# kernel module Makefile：交給目前執行中核心版本的 Kbuild 系統編譯。
obj-m := mq_module.o shm_module.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
