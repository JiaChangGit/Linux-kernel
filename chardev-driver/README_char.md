# Character Device Driver Demo

## Environment Check

```bash
uname -r
# Check current Linux kernel version
# Example: 5.15.0-xxx

lsb_release -a
# Confirm Ubuntu 22.04
```

---

# Step 1: Install Required Packages

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    linux-headers-$(uname -r) \
    kmod \
    gcc \
    make
```

---

# Step 2: Build Kernel Module

```bash
chmod +x scripts/*.sh

cd driver
make

ls -lh chardev.ko
# Verify kernel module was generated successfully
```

Expected output example:

```bash
-rw-r--r-- 1 user user 120K May 10 12:00 chardev.ko
```

---

# Step 3: Load Driver

```bash
cd ..
bash scripts/load.sh
```

---

# Step 4: Build Userspace Test Program

```bash
cd userspace
make
```

---

# Demo 1: Basic Write / Read

```bash
echo "Firmware Engineer" > /dev/chardev0
# Write data into device

cat /dev/chardev0
# Read data from device
```

Expected output:

```bash
Firmware Engineer
```

---

# Demo 2: procfs Status Monitoring

```bash
cat /proc/chardev_info
```

Expected output:

```bash
=== chardev driver status ===
buf_len : 18
read_only : 0
open_count : 2
read_count : 1
write_count: 1
buf_content: Firmware Engineer
```

---

# Demo 3: sysfs Attribute Operations

## Show All sysfs Attributes

```bash
ls /sys/class/chardev/chardev0/
```

## Read Buffer Length

```bash
cat /sys/class/chardev/chardev0/buf_len
```

## Read Statistics

```bash
cat /sys/class/chardev/chardev0/stats
```

## Enable Read-Only Mode via sysfs

```bash
echo 1 | sudo tee /sys/class/chardev/chardev0/read_only
```

Try writing again:

```bash
echo "test" > /dev/chardev0
```

Expected behavior:

```bash
Permission denied
```

## Disable Read-Only Mode

```bash
echo 0 | sudo tee /sys/class/chardev/chardev0/read_only
```

---

# Demo 4: Run Full ioctl Test Program

```bash
cd userspace
sudo ./test_app
```

---

# Demo 5: View Kernel Logs

```bash
sudo dmesg | grep chardev
```

Expected output:

```bash
[chardev] major=240 minor=0
[chardev] driver loaded successfully
[chardev] open() called, total opens: 1
[chardev] write() 18 bytes
[chardev] read() 18 bytes
[chardev] ioctl: get_len = 18
[chardev] ioctl: set_rdonly = 1
[chardev] write() blocked: read-only mode
```

---

# Demo 6: Unload Driver

```bash
cd ..
bash scripts/unload.sh
```

---

# Project Structure

```text
chardev-driver/
├── docs/
│   ├── ......
│
├── driver/
│   ├── chardev.c
│   │   # Main driver:
│   │   # cdev + ioctl + procfs + sysfs
│   │
│   ├── chardev.h
│   │   # Shared header:
│   │   # ioctl command definitions
│   │
│   └── Makefile
│
├── userspace/
│   ├── test_app.c
│   │   # Userspace test program:
│   │   # read/write/ioctl tests
│   │
│   └── Makefile
│
├── scripts/
│   ├── load.sh
│   │   # Load kernel module
│   │   # Create /dev node
│   │
│   └── unload.sh
│       # Remove kernel module
│       # Clean device node
│
└── README_char.md
```

---

---

# 字元裝置驅動 Demo

## 環境確認

```bash
uname -r
# 確認目前 Linux kernel 版本
# 例如：5.15.0-xxx

lsb_release -a
# 確認 Ubuntu 22.04
```

---

# Step 1：安裝編譯環境

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    linux-headers-$(uname -r) \
    kmod \
    gcc \
    make
```

---

# Step 2：編譯 Kernel Module

```bash
chmod +x scripts/*.sh

cd driver
make

ls -lh chardev.ko
# 確認 kernel module 是否成功產生
```

預期輸出：

```bash
-rw-r--r-- 1 user user 120K May 10 12:00 chardev.ko
```

---

# Step 3：載入 Driver

```bash
cd ..
bash scripts/load.sh
```

---

# Step 4：編譯 Userspace 測試程式

```bash
cd userspace
make
```

---

# 演示 1：基本 write / read

```bash
echo "Firmware Engineer" > /dev/chardev0
# 寫入資料到 device

cat /dev/chardev0
# 從 device 讀取資料
```

預期輸出：

```bash
Firmware Engineer
```

---

# 演示 2：procfs 狀態檢視

```bash
cat /proc/chardev_info
```

預期輸出：

```bash
=== chardev driver status ===
buf_len : 18
read_only : 0
open_count : 2
read_count : 1
write_count: 1
buf_content: Firmware Engineer
```

---

# 演示 3：sysfs 屬性操作

## 查看所有 sysfs 屬性

```bash
ls /sys/class/chardev/chardev0/
```

## 讀取 buffer 長度

```bash
cat /sys/class/chardev/chardev0/buf_len
```

## 查看統計資訊

```bash
cat /sys/class/chardev/chardev0/stats
```

## 透過 sysfs 開啟唯讀模式

```bash
echo 1 | sudo tee /sys/class/chardev/chardev0/read_only
```

再次嘗試寫入：

```bash
echo "test" > /dev/chardev0
```

預期行為：

```bash
Permission denied
```

## 關閉唯讀模式

```bash
echo 0 | sudo tee /sys/class/chardev/chardev0/read_only
```

---

# 演示 4：執行完整 ioctl 測試程式

```bash
cd ..
cd userspace
sudo ./test_app
```

---

# 演示 5：查看 Kernel Log

```bash
sudo dmesg | grep chardev
```

預期輸出：

```bash
[chardev] major=240 minor=0
[chardev] driver loaded successfully
[chardev] open() called, total opens: 1
[chardev] write() 18 bytes
[chardev] read() 18 bytes
[chardev] ioctl: get_len = 18
[chardev] ioctl: set_rdonly = 1
[chardev] write() blocked: read-only mode
```

---

# 演示 6：卸載 Driver

```bash
cd ..
bash scripts/unload.sh
```

---

# 專案結構

```text
chardev-driver/
├── docs/
│   ├── ......
│
├── driver/
│   ├── chardev.c
│   │   # 主驅動：
│   │   # cdev + ioctl + procfs + sysfs
│   │
│   ├── chardev.h
│   │   # 共用 header：
│   │   # ioctl 命令定義
│   │
│   └── Makefile
│
├── userspace/
│   ├── test_app.c
│   │   # Userspace 測試程式：
│   │   # read/write/ioctl 測試
│   │
│   └── Makefile
│
├── scripts/
│   ├── load.sh
│   │   # 載入 kernel module
│   │   # 建立 /dev 節點
│   │
│   └── unload.sh
│       # 卸載 kernel module
│       # 清除 device node
│
└── README_char.md
```
