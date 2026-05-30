# userspace 測試程式建置設定。
# -Wall / -Wextra 用來提早發現未檢查回傳值、型別不一致等問題。
CC      = gcc
CFLAGS  = -Wall -Wextra -I../driver

.PHONY: all clean

all: test_app

test_app: test_app.c ../driver/chardev.h
	$(CC) $(CFLAGS) -o test_app test_app.c

clean:
	rm -f test_app
