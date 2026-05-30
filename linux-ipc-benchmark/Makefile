# 專案根目錄 Makefile：統一轉呼叫 kernel 與 user 子目錄的建置規則。
.PHONY: all kernel user clean

all: kernel user

kernel:
	$(MAKE) -C kernel

user:
	$(MAKE) -C user

clean:
	$(MAKE) -C kernel clean
	$(MAKE) -C user   clean
