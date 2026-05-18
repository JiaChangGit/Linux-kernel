# 韌體工程師面試完全攻略
## 台灣科技業 Linux Kernel / Embedded Systems 面試問答整理

> 適用職位：韌體工程師、BSP 工程師、嵌入式 Linux 工程師、驅動程式工程師
> 涵蓋範圍：Linux Kernel、Driver、Memory、Process、即時系統、通訊協定、Build System

---

# 目錄

1. [記憶體管理](#一記憶體管理)
2. [行程與排程](#二行程與排程)
3. [驅動程式開發](#三驅動程式開發)
4. [中斷與同步機制](#四中斷與同步機制)
5. [裝置樹與平台驅動](#五裝置樹與平台驅動)
6. [檔案系統與 VFS](#六檔案系統與-vfs)
7. [通訊協定](#七通訊協定)
8. [Boot 流程與 Bootloader](#八boot-流程與-bootloader)
9. [即時系統 RTOS](#九即時系統-rtos)
10. [Build System 與工具鏈](#十build-system-與工具鏈)
11. [除錯與效能分析](#十一除錯與效能分析)
12. [電源管理](#十二電源管理)
13. [安全機制](#十三安全機制)
14. [C 語言與底層知識](#十四c-語言與底層知識)

---

# 一、記憶體管理

---

## Q1. 描述 `mmap` 的實作原理

### 回答

`mmap()` 的核心作用是將一段檔案或匿名記憶體映射到使用者空間的虛擬位址區間，讓程序能像存取陣列一樣直接讀寫頁面，完全不需要經過 `read()/write()` 的資料複製路徑。

#### 完整核心流程

```
使用者呼叫 mmap(fd, offset, length, prot, flags)
        |
        v
sys_mmap() -> do_mmap()
        |
        v
建立 vm_area_struct (VMA)
  - vm_start / vm_end
  - vm_flags (READ/WRITE/EXEC)
  - vm_ops (fault handler)
        |
        v
【此時尚未配置實體頁面 — Lazy Allocation】
        |
        v
程序第一次存取該虛擬位址
        |
        v
Page Fault 觸發
        |
        v
Kernel 根據 VMA 類型判斷：
  - 檔案映射 → 從 Page Cache 或 disk 載入
  - 匿名映射 → 配置新 zero page
        |
        v
建立 Page Table Entry (PTE)
        |
        v
程序可正常存取頁面
```

#### 關鍵特性詳解

| 特性 | 說明 |
|---|---|
| **Demand Paging** | 只在真正存取時才載入頁面，節省記憶體 |
| **Zero-copy** | 檔案讀取不需從 kernel buffer 複製到 user buffer |
| **Copy-On-Write (COW)** | `fork()` 後父子共享頁面，寫入時才複製 |
| **Shared Mapping** | `MAP_SHARED`：多個 process 共享同一實體頁面 |
| **Private Mapping** | `MAP_PRIVATE`：寫入時觸發 COW，互不影響 |

#### 常見面試追問

**Q: mmap 與 read() 的差別？**
A: `read()` 需要先從 disk 讀進 kernel page cache，再 copy 到 user buffer（兩次複製）。`mmap` 直接將 page cache 映射到 user space，只有一次映射，I/O 密集場景效能更好。

**Q: 什麼是 Huge TLB？**
A: 使用 2MB 或 1GB 大頁面替代 4KB 頁面，減少 TLB miss 次數，適合記憶體密集應用。

---

## Q2. `kmalloc` 與 `vmalloc` 有何不同？何時用哪個？

### 回答

```c
/* kmalloc：實體連續 */
void *p = kmalloc(size, GFP_KERNEL);

/* vmalloc：虛擬連續，實體不連續 */
void *p = vmalloc(size);
```

| 比較項目 | `kmalloc` | `vmalloc` |
|---|---|---|
| 虛擬位址 | 連續 | 連續 |
| 實體位址 | **連續** | **不連續** |
| 大小限制 | 通常 ≤ 4MB | 可更大 |
| 效能 | 較快（無需建 page table） | 較慢 |
| DMA 適用 | ✅ 適合 | ❌ 不適合 |
| 適用場景 | Driver、小型結構 | 大型 buffer、模組 |

#### 選擇原則

- 需要 **DMA** 操作 → 一定用 `kmalloc`（或 `dma_alloc_coherent`）
- 大小 > 幾百 KB → 考慮 `vmalloc`
- 效能敏感的 driver → `kmalloc`

---

## Q3. `kzalloc` 與 `kmalloc` 有何不同？

### 回答

```c
/* kmalloc：只配置，不清零 */
ptr = kmalloc(size, GFP_KERNEL);

/* kzalloc：配置 + memset(0) */
ptr = kzalloc(size, GFP_KERNEL);
/* 等同於 */
ptr = kmalloc(size, GFP_KERNEL);
memset(ptr, 0, size);
```

**為什麼清零很重要？**

1. **避免 kernel info leak**：未清零的 kernel 記憶體可能殘留敏感資訊，若暴露給 userspace 是安全漏洞
2. **避免 garbage value**：結構體成員可能有預設為 0 的語意（如 pointer 初始化為 NULL）
3. **CVE 案例**：歷史上有多個 CVE 就是因為未清零的 kernel buffer 洩漏資訊

**最佳實踐**：Driver 開發幾乎都應使用 `kzalloc`，除非確認不需要清零且效能極度敏感。

---

## Q4. `GFP_KERNEL` 與 `GFP_ATOMIC` 有何差異？

### 回答

GFP = **Get Free Pages** flags，告知核心記憶體配置的行為限制。

```c
/* GFP_KERNEL：可睡眠，一般 context */
ptr = kmalloc(size, GFP_KERNEL);

/* GFP_ATOMIC：不可睡眠，中斷/spinlock context */
ptr = kmalloc(size, GFP_ATOMIC);
```

| Flag | 可睡眠 | 可 reclaim | 適用 Context |
|---|---|---|---|
| `GFP_KERNEL` | ✅ | ✅ | 一般 kernel context |
| `GFP_ATOMIC` | ❌ | ❌ | ISR、spinlock 持鎖期間 |
| `GFP_NOWAIT` | ❌ | ❌ | 類似 ATOMIC |
| `GFP_DMA` | ✅ | ✅ | DMA 區域記憶體 |

**使用 GFP_ATOMIC 的場景：**
- Interrupt Service Routine (ISR)
- Tasklet
- 持有 spinlock 期間
- Timer callback

**為何不能在 ISR 用 GFP_KERNEL？**
因為 GFP_KERNEL 可能觸發記憶體回收，進而 sleep/block，而 ISR 必須在 interrupt context 中快速執行，不允許任何形式的睡眠。

---

## Q5. 什麼是實體位址與虛擬位址？MMU 如何運作？

### 回答

#### 位址空間概念

```
使用者程序 A         使用者程序 B         Kernel
[0x0000 - 0x7FFF]   [0x0000 - 0x7FFF]   [0xC000 - 0xFFFF]
  虛擬位址空間         虛擬位址空間        虛擬位址空間
       |                   |                   |
       +-------------------+-------------------+
                           |
                        MMU 轉換
                           |
                    實體 RAM 位址空間
              [0x0000_0000 - 0x(最大RAM)]
```

#### MMU（Memory Management Unit）運作機制

1. **Page Table**：每個 process 有自己的 page table，儲存 VA → PA 的映射關係
2. **TLB（Translation Lookaside Buffer）**：Page table 的快取，避免每次都查記憶體
3. **Page Fault**：存取不在 page table 或尚未映射的位址時觸發，由 kernel 處理

#### 轉換流程

```
CPU 發出虛擬位址 (VA)
        |
查詢 TLB → TLB Hit → 直接得到實體位址 (PA)
        |
    TLB Miss
        |
查詢 Page Table（軟體或硬體 PTW）
        |
找到 PTE → 更新 TLB → 得到 PA
        |
    PA 不存在 → Page Fault
```

#### Linux 的記憶體配置（以 32-bit ARM 為例）

```
0x0000_0000 - 0xBFFF_FFFF : User Space (3GB)
0xC000_0000 - 0xFFFF_FFFF : Kernel Space (1GB)
```

---

## Q6. 什麼是 HugePages？有什麼優缺點？

### 回答

HugePages 使用比標準 4KB 更大的頁面：
- **x86**：2MB（Transparent HugePage, THP）、1GB（Huge 1GB）
- **ARM**：2MB、1GB

#### 優點
- **降低 TLB miss**：同樣記憶體用更少 TLB entry
- **減少 Page Table 層次**：減少記憶體 overhead
- **提升大記憶體應用效能**：資料庫（如 MySQL、Redis）常用

#### 缺點
- **記憶體碎片化**：大頁面難以找到連續實體記憶體
- **小應用浪費**：小程式配到 2MB 但只用幾 KB

#### 啟用方式

```bash
# Transparent HugePage（自動）
echo always > /sys/kernel/mm/transparent_hugepage/enabled

# 明確大頁面
echo 512 > /proc/sys/vm/nr_hugepages
```

---

## Q7. `copy_from_user` 為何不能直接用指標存取 User Space？

### 回答

**直接存取的風險：**

```c
/* 危險！不要這樣做 */
char c = *user_ptr;
```

1. **Page Fault**：User 指標指向的頁面可能未載入或被 swap out
2. **非法位址**：使用者可能傳入任意位址（包含 kernel 位址），造成安全漏洞
3. **TOCTOU 攻擊**：Time-of-check to time-of-use，使用者可在檢查後修改記憶體

**正確做法：**

```c
/* copy_from_user：安全從 user 複製到 kernel */
if (copy_from_user(&kernel_buf, user_ptr, size)) {
    return -EFAULT;  /* 失敗時回傳 EFAULT */
}

/* copy_to_user：安全從 kernel 複製到 user */
if (copy_to_user(user_ptr, &kernel_buf, size)) {
    return -EFAULT;
}
```

**這些 API 做了什麼：**
1. 驗證 user pointer 是否在合法 user space 範圍
2. 使用特殊例外處理（`__ex_table`）捕捉可能的 page fault
3. 確保不會因為非法存取造成 kernel panic

---

# 二、行程與排程

---

## Q8. `fork()` 的回傳值與 Copy-On-Write 機制

### 回答

```c
pid_t pid = fork();

if (pid < 0) {
    /* 失敗 */
    perror("fork");
} else if (pid == 0) {
    /* 子行程：回傳 0 */
    printf("I am child, my PID = %d\n", getpid());
} else {
    /* 父行程：回傳子行程的 PID */
    printf("I am parent, child PID = %d\n", pid);
}
```

#### Copy-On-Write（COW）機制

`fork()` 後父子行程共享相同的實體頁面，但 page table 標記為**唯讀**。

```
fork() 後：
父行程 VMA ──┐
             ├──> 共享實體頁面（只讀映射）
子行程 VMA ──┘

當任一方寫入：
        寫入觸發 Page Fault
               |
        Kernel 複製該頁面
               |
        各自擁有獨立的實體頁面
```

**COW 的好處：**
- `fork()` 後若立刻 `exec()`（如 shell），完全不需要複製任何資料
- 大幅降低 `fork()` 的成本

---

## Q9. 什麼是殭屍行程（Zombie Process）？如何避免？

### 回答

**殭屍行程定義：**
子行程已結束，但父行程尚未呼叫 `wait()` 收割，子行程的 PCB（Process Control Block）仍殘留在系統中。

```
子行程執行 exit()
       |
進入 Zombie 狀態（Z）
       |
父行程呼叫 wait() → 清除 Zombie
```

**為什麼有問題：**
Zombie 持續佔用 PID，若大量累積可能耗盡 PID 資源（Linux 預設最大 PID 約 32768）。

**解決方法：**

```c
/* 方法一：直接 wait */
waitpid(child_pid, &status, 0);

/* 方法二：SIGCHLD 信號處理 */
signal(SIGCHLD, SIG_IGN);  /* 讓 kernel 自動回收 */

/* 方法三：非同步 waitpid */
void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}
signal(SIGCHLD, sigchld_handler);

/* 方法四：雙重 fork（子行程孤兒化由 init 收割）*/
if (fork() == 0) {
    if (fork() == 0) {
        /* 孫行程，parent 是 init */
        execv(...);
    }
    exit(0);  /* 子行程立即結束 */
}
wait(NULL);  /* 父行程等待子行程 */
```

---

## Q10. Linux 排程演算法：CFS 是如何運作的？

### 回答

Linux 的預設排程器是 **CFS（Completely Fair Scheduler）**，目標是給每個 task 公平的 CPU 時間。

#### 核心概念：Virtual Runtime（vruntime）

```
vruntime 增量 = 實際執行時間 × (NICE_0_LOAD / task 優先級權重)
```

- 優先級越高（nice 值越低）→ vruntime 增長越慢 → 更常被排程
- 永遠選擇 **vruntime 最小**的 task 執行

#### 資料結構：Red-Black Tree

```
CFS 用 Red-Black Tree 管理所有 runnable tasks
最左邊節點 = vruntime 最小 = 下一個執行的 task
```

```
           [vrt=100]
          /          \
    [vrt=80]       [vrt=120]
    /
[vrt=60]  <-- 下次執行這個
```

#### Round Robin vs CFS vs Real-Time

| 演算法 | 特性 | 適用場景 |
|---|---|---|
| Round Robin | 固定時間片輪轉，公平 | 時分系統 |
| CFS | 動態時間片，按權重公平 | 一般 Linux 桌面/伺服器 |
| FIFO (RT) | 不可搶佔，優先級最高 | 硬即時任務 |
| RR (RT) | 同優先級輪轉 | 軟即時任務 |

#### 排程類別優先順序

```
Stop → Deadline → Realtime → CFS → Idle
```

---

## Q11. 什麼是 Context Switch？成本是什麼？

### 回答

Context Switch 是 CPU 從執行一個 task 切換到另一個 task 的過程。

#### 步驟

```
1. 儲存目前 task 狀態（暫存器、PC、stack pointer）
2. 更新 PCB（Process Control Block）
3. 切換 page table（若跨 process）→ TLB flush
4. 載入下一個 task 狀態
5. 恢復執行
```

#### 成本來源

| 成本項目 | 說明 |
|---|---|
| 直接成本 | 儲存/恢復暫存器（幾十到幾百 ns） |
| TLB flush | Process 切換需清除 TLB（微秒級） |
| Cache cold | 新 task 的資料不在 cache，大量 cache miss |
| Pipeline flush | 分支預測失效 |

**Thread vs Process Context Switch：**
同一 process 的 thread 切換不需要切換 page table，成本遠低於 process 切換。

---

# 三、驅動程式開發

---

## Q12. Linux 字元裝置完整註冊流程

### 回答

```c
/* 完整的字元驅動程式框架 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "mychardev"
#define CLASS_NAME  "myclass"

static int    major_number;
static struct class  *dev_class;
static struct device *dev_device;
static struct cdev    my_cdev;

/* Step 1: file_operations */
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

static const struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = dev_open,
    .release = dev_release,
    .read    = dev_read,
    .write   = dev_write,
};

static int __init chardev_init(void)
{
    dev_t dev;

    /* Step 2: 配置裝置號碼 */
    alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    major_number = MAJOR(dev);

    /* Step 3: 初始化 cdev */
    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;

    /* Step 4: 註冊 cdev 到 kernel */
    cdev_add(&my_cdev, dev, 1);

    /* Step 5: 建立 sysfs class */
    dev_class = class_create(THIS_MODULE, CLASS_NAME);

    /* Step 6: 建立 /dev/mychardev 節點 */
    dev_device = device_create(dev_class, NULL, dev, NULL, DEVICE_NAME);

    pr_info("mychardev: registered with major %d\n", major_number);
    return 0;
}

static void __exit chardev_exit(void)
{
    dev_t dev = MKDEV(major_number, 0);
    device_destroy(dev_class, dev);
    class_destroy(dev_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev, 1);
}

module_init(chardev_init);
module_exit(chardev_exit);
MODULE_LICENSE("GPL");
```

#### 流程圖

```
alloc_chrdev_region()   → 取得 major/minor number
        |
cdev_init(&cdev, &fops) → 綁定 file_operations
        |
cdev_add()              → 向 VFS 註冊
        |
class_create()          → 建立 /sys/class/myclass
        |
device_create()         → 建立 /dev/mychardev
        |
使用者可 open/read/write/ioctl
```

---

## Q13. `ioctl` 的 Magic Number 是什麼？如何正確定義？

### 回答

`ioctl` 命令碼由 4 個欄位組成：

```
31       30-29     28-16       15-8      7-0
[方向位]  [size]   [type/magic] [序號]
```

| 欄位 | 說明 |
|---|---|
| dir（2 bits） | _IO / _IOR / _IOW / _IOWR |
| size（14 bits） | 傳遞資料的大小 |
| type（8 bits） | Magic Number，區分 driver |
| nr（8 bits） | 命令序號 |

```c
/* 定義 Magic Number */
#define MY_MAGIC 'M'

/* 無資料傳遞 */
#define MY_IOCTL_RESET    _IO(MY_MAGIC, 0)

/* 從 kernel 讀取資料到 user */
#define MY_IOCTL_GET_VAL  _IOR(MY_MAGIC, 1, int)

/* 從 user 寫入資料到 kernel */
#define MY_IOCTL_SET_VAL  _IOW(MY_MAGIC, 2, int)

/* 雙向 */
#define MY_IOCTL_RW       _IOWR(MY_MAGIC, 3, struct my_data)

/* Driver 端處理 */
static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int val;
    switch (cmd) {
    case MY_IOCTL_GET_VAL:
        val = get_hardware_value();
        if (copy_to_user((int __user *)arg, &val, sizeof(val)))
            return -EFAULT;
        break;
    case MY_IOCTL_SET_VAL:
        if (copy_from_user(&val, (int __user *)arg, sizeof(val)))
            return -EFAULT;
        set_hardware_value(val);
        break;
    default:
        return -ENOTTY;  /* 未知命令 */
    }
    return 0;
}
```

**Magic Number 的重要性：**
避免不同 driver 的 ioctl 命令碼衝突，若一個程式誤用了另一個 driver 的 fd 發送 ioctl，Magic Number 不符會讓 kernel 或 driver 拒絕。

---

## Q14. 什麼是 Platform Driver？與一般 Driver 的差異？

### 回答

**Platform Device** 是不可熱插拔的裝置，通常是 SoC 上直接整合的周邊（UART、SPI 控制器、GPIO 等）。

```c
/* Platform Driver 範例 */
static int my_probe(struct platform_device *pdev)
{
    struct resource *res;

    /* 從 Device Tree 取得 MMIO 資源 */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    base = devm_ioremap_resource(&pdev->dev, res);

    /* 取得 IRQ */
    irq = platform_get_irq(pdev, 0);
    devm_request_irq(&pdev->dev, irq, my_isr, 0, "my_irq", NULL);

    return 0;
}

static int my_remove(struct platform_device *pdev)
{
    /* 清理資源（devm_* 系列自動管理） */
    return 0;
}

static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-device" },
    { }
};

static struct platform_driver my_driver = {
    .probe  = my_probe,
    .remove = my_remove,
    .driver = {
        .name           = "my_device",
        .of_match_table = my_of_match,
    },
};

module_platform_driver(my_driver);
```

| 比較 | Platform Driver | USB/PCI Driver |
|---|---|---|
| 裝置描述 | Device Tree | 自動枚舉（熱插拔） |
| 匹配方式 | compatible string | Vendor/Product ID |
| 適合裝置 | SoC 內建周邊 | 外部可插拔裝置 |

---

## Q15. `devm_*` 系列函式有什麼好處？

### 回答

`devm_` 前綴表示 **Device Managed Resource**，資源的生命週期綁定到 device，driver `remove` 或 `probe` 失敗時自動釋放。

```c
/* 傳統方式：需要手動在 remove() 中釋放 */
base = ioremap(res->start, resource_size(res));
irq = request_irq(...);
// remove() 必須對應 iounmap() 和 free_irq()

/* devm 方式：自動管理 */
base = devm_ioremap_resource(&pdev->dev, res);
devm_request_irq(&pdev->dev, irq, handler, 0, "name", NULL);
// remove() 不需要手動清理！
```

**常用 devm API：**

| API | 對應釋放 |
|---|---|
| `devm_kmalloc()` | 自動 kfree |
| `devm_ioremap()` | 自動 iounmap |
| `devm_request_irq()` | 自動 free_irq |
| `devm_clk_get()` | 自動 clk_put |
| `devm_gpio_request()` | 自動 gpio_free |

**好處：**
1. 避免 `probe()` 中途失敗時資源洩漏
2. 簡化 `remove()` 程式碼
3. 減少 bug

---

# 四、中斷與同步機制

---

## Q16. 中斷上半部與下半部（Top Half / Bottom Half）

### 回答

中斷處理分為兩個階段，核心原則：**上半部越快越好，下半部處理實際工作**。

```
硬體中斷觸發
      |
   Top Half（ISR）
   - 關中斷或 mask IRQ
   - 清除 interrupt flag
   - 讀取硬體狀態到 buffer
   - 排程 Bottom Half
      |
   返回（中斷重新啟用）
      |
   Bottom Half（延後執行）
   ├── softirq   → 預先靜態定義，可並行（不同 CPU）
   ├── tasklet   → 建立在 softirq 之上，同時只有一個執行
   └── workqueue → 在 process context 執行，可以 sleep
```

#### 三種 Bottom Half 比較

| 機制 | 執行 Context | 可睡眠 | 並行性 | 適用場景 |
|---|---|---|---|---|
| softirq | Interrupt context | ❌ | 多 CPU 並行 | 高頻網路、區塊 I/O |
| tasklet | Interrupt context | ❌ | 同時只一個 | 一般 driver |
| workqueue | Process context | ✅ | 可設定並行 | 需要睡眠的延後工作 |

#### 程式範例

```c
/* tasklet */
DECLARE_TASKLET(my_tasklet, my_tasklet_func, 0);

static irqreturn_t my_isr(int irq, void *dev)
{
    /* Top Half：快速處理 */
    raw_data = readl(base + DATA_REG);
    writel(IRQ_CLEAR, base + STATUS_REG);

    /* 排程 Bottom Half */
    tasklet_schedule(&my_tasklet);
    return IRQ_HANDLED;
}

static void my_tasklet_func(unsigned long data)
{
    /* Bottom Half：處理資料 */
    process_data(raw_data);
}

/* workqueue（可睡眠版） */
INIT_WORK(&my_work, my_work_func);

static irqreturn_t my_isr(int irq, void *dev)
{
    schedule_work(&my_work);
    return IRQ_HANDLED;
}

static void my_work_func(struct work_struct *work)
{
    /* 可以 sleep、mutex、GFP_KERNEL */
    msleep(10);
    mutex_lock(&my_mutex);
}
```

---

## Q17. `spinlock` 與 `mutex` 的差異，何時使用哪個？

### 回答

#### 核心差異

| 比較 | spinlock | mutex |
|---|---|---|
| 等待方式 | Busy waiting（一直 spin） | 睡眠等待（yield CPU） |
| 持鎖期間可否睡眠 | ❌ 絕對不行 | ✅ 可以 |
| 適用 Context | IRQ、softirq、任意 | Process context only |
| 持鎖時間 | 極短（幾十 cycle） | 可以較長 |
| 多核效率 | 短臨界區優秀 | 長臨界區優秀 |

#### 為何 spinlock 不能睡眠？

```
CPU0 持有 spinlock
    |
CPU0 執行了 sleep（被排程出去）
    |
CPU1 嘗試取得 spinlock → 一直 spin
    |
但 CPU0 需要 CPU1 完成某事才能 wake up
    |
死鎖！（Deadlock）
```

#### 使用原則

```c
/* spinlock：中斷 context 或超短臨界區 */
spinlock_t lock;
spin_lock_irqsave(&lock, flags);
/* 極短的臨界區 */
spin_unlock_irqrestore(&lock, flags);

/* mutex：process context，可睡眠 */
struct mutex my_mutex;
mutex_init(&my_mutex);
mutex_lock(&my_mutex);
/* 允許睡眠的臨界區 */
mutex_unlock(&my_mutex);
```

---

## Q18. 什麼是 RCU（Read-Copy-Update）？

### 回答

RCU 是一種針對**讀多寫少**場景優化的同步機制，Reader 完全無鎖、零開銷。

#### 核心思想

```
Reader：
  rcu_read_lock()     /* 只是 preemption disable */
  p = rcu_dereference(gp)  /* 讀取指標 */
  使用 p
  rcu_read_unlock()

Writer：
  new_p = kmalloc(...)     /* 建立新版本 */
  *new_p = *old_p          /* 複製並修改 */
  new_p->value = new_val

  rcu_assign_pointer(gp, new_p)  /* 原子替換指標 */
  synchronize_rcu()              /* 等待所有 Reader 離開 */
  kfree(old_p)                   /* 安全釋放舊版本 */
```

#### Grace Period 概念

```
Writer 替換指標後，等待所有 CPU 都經歷過一次排程（Grace Period）
-> 保證沒有 Reader 還在使用舊指標
-> 才能釋放舊資料
```

**適用場景：**
- Kernel routing table
- Module list
- 任何讀遠多於寫的資料結構

---

# 五、裝置樹與平台驅動

---

## Q19. 什麼是 Device Tree？解決了什麼問題？

### 回答

**問題背景（ARM 的 Board File 災難）：**
ARM 早期每個板子都要一個獨立的 `board-xxx.c`，裡面寫死了所有硬體資訊（GPIO、IRQ、MMIO），導致 kernel 充滿幾百個幾乎相同的板子檔案，Linus 大罵這是 "This whole ARM thing is a fucking pain in the ass"。

**Device Tree 解法：**
將硬體描述從 kernel 抽出，放入 `.dts` 文字檔，編譯成 `.dtb` 二進制，Bootloader 載入並傳給 kernel。

```dts
/* 範例 .dts */
/ {
    compatible = "vendor,my-board";

    memory@80000000 {
        device_type = "memory";
        reg = <0x80000000 0x40000000>; /* 1GB RAM */
    };

    uart0: serial@10000000 {
        compatible = "vendor,my-uart";
        reg = <0x10000000 0x1000>;
        interrupts = <GIC_SPI 5 IRQ_TYPE_LEVEL_HIGH>;
        clocks = <&uart_clk>;
        status = "okay";
    };

    i2c0: i2c@20000000 {
        compatible = "vendor,my-i2c";
        reg = <0x20000000 0x100>;
        #address-cells = <1>;
        #size-cells = <0>;

        /* I2C 裝置 */
        sensor@48 {
            compatible = "ti,tmp102";
            reg = <0x48>;
        };
    };
};
```

**好處：**
- Driver 與硬體資訊分離
- 一份 kernel 支援多種板子
- 只需更換 `.dtb` 檔案

---

## Q20. Kernel 如何透過 `compatible` 字串匹配 Driver？

### 回答

```
Device Tree：
    compatible = "vendor,my-uart";

Driver 的 of_match_table：
    { .compatible = "vendor,my-uart" }

Kernel 啟動時：
    掃描 DT 所有節點 → 找 compatible → 對比所有 driver 的 of_match_table
    → 找到匹配 → 呼叫 driver 的 probe() 函式
```

```c
/* Driver 端 */
static const struct of_device_id my_uart_of_match[] = {
    { .compatible = "vendor,my-uart",  .data = &my_uart_v1_data },
    { .compatible = "vendor,my-uart2", .data = &my_uart_v2_data },
    { }  /* 必須以空 entry 結尾 */
};
MODULE_DEVICE_TABLE(of, my_uart_of_match);

static int my_uart_probe(struct platform_device *pdev)
{
    const struct of_device_id *match;
    match = of_match_node(my_uart_of_match, pdev->dev.of_node);

    /* 讀取 DT 屬性 */
    of_property_read_u32(pdev->dev.of_node, "clock-frequency", &clk_freq);

    return 0;
}
```

---

# 六、檔案系統與 VFS

---

## Q21. VFS（Virtual File System）的架構與關鍵資料結構

### 回答

VFS 是 Linux 的抽象層，讓所有檔案系統（ext4、FAT、proc、sysfs）呈現統一的介面。

```
應用程式：open("/proc/cpuinfo", O_RDONLY)
           |
        VFS 層
    ┌───────────────────────────────┐
    │  struct file        (已開啟的檔案)│
    │  struct dentry      (路徑名稱節點)│
    │  struct inode       (檔案 metadata)│
    │  struct super_block (檔案系統實例) │
    └───────────────────────────────┘
           |
    具體實作：ext4 / procfs / tmpfs ...
```

| 資料結構 | 說明 | 關鍵欄位 |
|---|---|---|
| `super_block` | 掛載的檔案系統 | s_op（super 操作）|
| `inode` | 檔案元資料（不含名稱） | i_mode, i_size, i_op |
| `dentry` | 路徑元件（目錄快取） | d_name, d_inode, d_parent |
| `file` | 行程開啟的檔案實例 | f_pos, f_op |

#### `open()` 系統呼叫後 kernel 發生的事

```
sys_open("/dev/mydev", O_RDWR)
    |
namei lookup（路徑解析）
    | dentry cache 查詢
    | 找到 inode
    |
建立 struct file
    | file->f_op = inode->i_fop
    |
呼叫 file->f_op->open() （driver 的 open callback）
    |
分配 file descriptor
    | fd = get_unused_fd()
    | fd_install(fd, file)
    |
回傳 fd 給使用者
```

---

## Q22. 什麼是 `seq_file`？為何要用它？

### 回答

`seq_file` 解決了傳統 `/proc` 輸出的問題：若資料超過一個 page（4KB），`read()` 只能讀到一部分。

```c
/* 傳統 proc read（有問題）*/
static int my_read(char *buf, char **start, off_t off, int count, ...)
{
    /* 如果資料 > count，就麻煩了 */
    len = sprintf(buf, ...);  /* 可能 overflow */
    return len;
}

/* seq_file 正確做法 */
static int my_seq_show(struct seq_file *s, void *v)
{
    seq_printf(s, "value = %d\n", my_value);
    seq_printf(s, "status = %s\n", my_status);
    return 0;
}

static int my_seq_open(struct inode *inode, struct file *file)
{
    return single_open(file, my_seq_show, NULL);
}

static const struct file_operations my_fops = {
    .open    = my_seq_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};
```

**`seq_file` 的優點：**
- 自動處理 `read()` 分批讀取
- 正確處理 offset
- 支援大量輸出而不溢出

---

# 七、通訊協定

---

## Q23. I2C 通訊協定詳解

### 回答

I2C（Inter-Integrated Circuit）是兩線式同步串列通訊協定。

**訊號線：**
- **SDA**（Serial Data）：資料線，雙向
- **SCL**（Serial Clock）：時鐘線，由 Master 控制

**特性：**
- 多 Master / 多 Slave（7-bit 或 10-bit 位址）
- 速度：100kHz（標準）、400kHz（Fast）、1MHz（Fast+）、3.4MHz（High-Speed）
- 需要 Pull-up 電阻（Open-Drain 設計）

**通訊時序：**

```
START  A6 A5 A4 A3 A2 A1 A0  R/W  ACK  D7...D0  ACK  STOP
  |    [  7-bit 裝置位址    ]  [讀/寫] [ACK]  [資料] [ACK]  |
```

**Linux I2C Driver 範例：**

```c
/* I2C 裝置讀取 */
static int my_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
{
    struct i2c_msg msgs[2];

    /* Write phase: 送出暫存器位址 */
    msgs[0].addr  = client->addr;
    msgs[0].flags = 0;  /* Write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    /* Read phase: 讀取資料 */
    msgs[1].addr  = client->addr;
    msgs[1].flags = I2C_M_RD;  /* Read */
    msgs[1].len   = 1;
    msgs[1].buf   = val;

    return i2c_transfer(client->adapter, msgs, 2);
}

/* 簡化寫法 */
i2c_smbus_read_byte_data(client, reg);
i2c_smbus_write_byte_data(client, reg, value);
```

---

## Q24. SPI 通訊協定詳解

### 回答

SPI（Serial Peripheral Interface）是四線式同步串列通訊協定。

**訊號線：**
- **SCLK**：時鐘（Master 控制）
- **MOSI**：Master Out, Slave In（主送）
- **MISO**：Master In, Slave Out（主收）
- **CS/SS**：Chip Select（低電位有效，每個 Slave 獨立）

**與 I2C 比較：**

| 比較 | I2C | SPI |
|---|---|---|
| 訊號線數 | 2（SDA, SCL） | 4（SCLK, MOSI, MISO, CS） |
| 速度 | 最高 3.4MHz | 可達數十 MHz |
| 多 Slave | 位址定址 | 獨立 CS 線 |
| 雙工 | 半雙工 | 全雙工 |
| 適用 | 短距、感測器、EEPROM | 高速 ADC、Flash、顯示器 |

**SPI 模式（CPOL/CPHA）：**

| Mode | CPOL | CPHA | 說明 |
|---|---|---|---|
| 0 | 0 | 0 | 時鐘閒置低，上升緣取樣 |
| 1 | 0 | 1 | 時鐘閒置低，下降緣取樣 |
| 2 | 1 | 0 | 時鐘閒置高，下降緣取樣 |
| 3 | 1 | 1 | 時鐘閒置高，上升緣取樣 |

---

## Q25. UART 通訊協定詳解

### 回答

UART（Universal Asynchronous Receiver/Transmitter）是最簡單的串列通訊。

**特性：**
- **非同步**：不需要共享時鐘線
- 兩線：TX（傳送）、RX（接收）
- 最常見格式：`115200 8N1`

**8N1 解碼：**
```
115200 → Baud Rate（每秒 115200 個符號）
8      → Data Bits（8 個資料位元）
N      → Parity（None，無同位檢查；也有 Even/Odd）
1      → Stop Bits（1 個停止位元）
```

**Frame 結構：**
```
IDLE  START  D0 D1 D2 D3 D4 D5 D6 D7  STOP  IDLE
 1     0     資料位元（LSB first）     1
```

**嵌入式常見用途：**
- Boot log 輸出
- Debug console（`/dev/ttyS0`）
- 與 GPS、藍牙模組通訊

---

## Q26. GPIO 操作與 Linux GPIO 子系統

### 回答

GPIO（General Purpose Input/Output）是最基本的數位訊號控制。

**Legacy GPIO API（舊，不建議）：**
```c
gpio_request(GPIO_NUM, "label");
gpio_direction_output(GPIO_NUM, 0);
gpio_set_value(GPIO_NUM, 1);
gpio_free(GPIO_NUM);
```

**新式 GPIO Descriptor API（建議）：**
```c
#include <linux/gpio/consumer.h>

/* 從 Device Tree 取得 GPIO */
struct gpio_desc *gpio;
gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
if (IS_ERR(gpio))
    return PTR_ERR(gpio);

/* 操作 */
gpiod_set_value(gpio, 1);   /* 高電位 */
gpiod_set_value(gpio, 0);   /* 低電位 */
val = gpiod_get_value(gpio); /* 讀取 */
```

**Device Tree 對應：**
```dts
my_device {
    compatible = "vendor,my-dev";
    reset-gpios = <&gpio1 5 GPIO_ACTIVE_LOW>;
    enable-gpios = <&gpio2 3 GPIO_ACTIVE_HIGH>;
};
```

**GPIO 中斷：**
```c
irq = gpiod_to_irq(gpio);
devm_request_irq(dev, irq, my_handler,
                 IRQF_TRIGGER_FALLING, "my_gpio_irq", NULL);
```

---

# 八、Boot 流程與 Bootloader

---

## Q27. 嵌入式 Linux 完整 Boot 流程

### 回答

```
上電 / Reset
      |
   BootROM（片上固化，不可修改）
   - 硬體最基本初始化
   - 決定啟動來源（eMMC/SD/NAND/USB）
   - 載入 Secondary Bootloader（SPL）
      |
   SPL / MLO（Secondary Program Loader）
   - 初始化 DDR SDRAM
   - 初始化 Clock / PLL
   - 載入完整 Bootloader（如 U-Boot）到 RAM
      |
   U-Boot（Bootloader）
   - 初始化週邊（Ethernet、USB、Flash）
   - 載入 kernel image（zImage/Image）到 RAM
   - 載入 Device Tree Blob（.dtb）到 RAM
   - 載入 initramfs（可選）
   - 設定 kernel 啟動參數（bootargs）
   - 呼叫 kernel entry point
      |
   Linux Kernel 啟動
   - 解壓縮自身（若 zImage）
   - 解析 Device Tree
   - 初始化 Memory、GIC、Timer
   - 啟動各子系統（mm, vfs, net...）
   - 載入 driver（match DT compatible）
   - 掛載 root filesystem
   - 執行 init（PID 1）
      |
   Userspace Init（systemd / BusyBox init）
   - 執行 /etc/init.d 或 systemd units
   - 啟動服務（網路、SSH、應用程式）
      |
   系統就緒
```

---

## Q28. U-Boot 常用指令與環境變數

### 回答

```bash
# 查看環境變數
printenv

# 設定啟動參數
setenv bootargs "console=ttyS0,115200 root=/dev/mmcblk0p2 rw"

# 儲存環境變數
saveenv

# 從 SD 卡載入 kernel
fatload mmc 0:1 ${loadaddr} zImage
fatload mmc 0:1 ${fdt_addr} my-board.dtb

# 啟動 kernel
bootz ${loadaddr} - ${fdt_addr}

# 網路開機（TFTP）
dhcp
tftpboot ${loadaddr} zImage
bootz ${loadaddr} - ${fdt_addr}

# 查看 Flash 分區
mtdparts
nand info

# 記憶體測試
mtest 0x80000000 0x81000000

# 燒錄 kernel 到 NAND
nand erase.part kernel
nand write ${loadaddr} kernel ${filesize}
```

#### 常見 bootargs 參數

```bash
console=ttyS0,115200    # 控制台
root=/dev/mmcblk0p2    # 根文件系統
rootfstype=ext4        # 文件系統類型
rw                     # 讀寫掛載
init=/sbin/init        # init 程序
panic=5                # kernel panic 後 5 秒重啟
loglevel=7             # 詳細 kernel log
```

---

# 九、即時系統 RTOS

---

## Q29. RTOS 與 GPOS（Linux）的核心差異

### 回答

| 比較項目 | RTOS | GPOS（Linux） |
|---|---|---|
| **核心目標** | 確定性、低延遲 | 高吞吐量、功能豐富 |
| **排程器** | Priority-based，可搶佔 | CFS（公平）+ RT |
| **中斷延遲** | 微秒等級保證 | 毫秒等級（不保證） |
| **Preemption** | 全搶佔 | 可設定 |
| **記憶體** | 靜態配置為主 | 動態、虛擬記憶體 |
| **例子** | FreeRTOS、VxWorks、QNX | Linux、Android |

#### Linux RT-Preempt Patch

Linux 可透過 `PREEMPT_RT` patch 達到接近硬即時的效能：
- 將 spinlock 替換為 rt_mutex（可排程）
- 中斷 handler 改為 kernel thread
- 理論最壞中斷延遲 < 100μs

```bash
# 查看目前 kernel preemption 設定
grep PREEMPT /boot/config-$(uname -r)
# CONFIG_PREEMPT_RT=y  ← 即時版本
```

---

## Q30. FreeRTOS 核心概念

### 回答

FreeRTOS 是最流行的嵌入式 RTOS，常見於 MCU（STM32、ESP32）。

**核心元件：**

```c
/* Task 建立 */
xTaskCreate(
    my_task_func,     /* 函式 */
    "MyTask",         /* 名稱 */
    1024,             /* Stack 大小（words）*/
    NULL,             /* 參數 */
    5,                /* 優先級（越高越優先）*/
    &task_handle      /* handle */
);

/* 佇列（Queue）- task 間通訊 */
QueueHandle_t q = xQueueCreate(10, sizeof(int));
xQueueSend(q, &val, portMAX_DELAY);
xQueueReceive(q, &val, portMAX_DELAY);

/* 信號量（Semaphore）*/
SemaphoreHandle_t sem = xSemaphoreCreateBinary();
xSemaphoreGive(sem);
xSemaphoreTake(sem, portMAX_DELAY);

/* Mutex */
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
xSemaphoreTake(mutex, portMAX_DELAY);
/* 臨界區 */
xSemaphoreGive(mutex);
```

**Task 狀態機：**
```
Running → Blocked（等待）
        → Suspended（明確暫停）
        → Ready（可執行，等待排程）
```

**Priority Inversion 問題：**
- 低優先 task 持鎖 → 高優先 task 等鎖 → 中優先 task 搶占低優先
- 解法：Priority Inheritance（mutex 持有者暫時升為最高優先）

---

## Q31. 什麼是 Interrupt Latency？如何測量與優化？

### 回答

Interrupt Latency = 硬體中斷觸發 → ISR 第一行執行的時間差

**來源分析：**

```
中斷觸發
   |
1. CPU pipeline flush + mode switch     (~幾 cycles)
2. 儲存 context（暫存器）               (~幾十 cycles)
3. 若中斷被 mask → 等待 unmask         (~可能很長！)
4. GIC/PIC 仲裁                        (~幾十 cycles)
5. 跳轉到 ISR handler
```

**優化方法：**

1. **縮短 interrupt disable 時間**（最重要）
2. **ISR 越短越好**：只做最必要的事，其餘交給 Bottom Half
3. **避免在 IRQ 路徑上做記憶體配置**
4. **CPU Affinity**：將中斷綁定到特定 CPU，避免 cache miss

**測量工具：**

```bash
# cyclictest：RTOS latency 測試工具
cyclictest -p 99 -t -n -i 200 -l 10000

# ftrace：追蹤中斷延遲
echo irqsoff > /sys/kernel/debug/tracing/current_tracer
```

---

# 十、Build System 與工具鏈

---

## Q32. Yocto Project 核心概念

### 回答

Yocto 是業界標準的嵌入式 Linux 建構系統，由 Linux Foundation 維護。

**核心概念：**

```
Layer（層）
  meta/         ← 核心 layer（OpenEmbedded-Core）
  meta-poky/    ← Poky distro layer
  meta-bsp/     ← 板子支援 layer（BSP）
  meta-myapp/   ← 自訂應用程式 layer
       |
       |  BitBake（建構引擎）
       |
  Recipe (.bb) → 描述如何下載、編譯、安裝一個軟體包
  bbappend     → 修改/擴充其他 layer 的 recipe
       |
       v
  rootfs image（ext4 / squashfs / wic）
  kernel image（zImage / Image）
  DTB 檔案
  SDK（可選）
```

**常用指令：**

```bash
# 初始化環境
source oe-init-build-env build

# 建構完整 image
bitbake core-image-minimal
bitbake core-image-full-cmdline

# 只建構某個套件
bitbake busybox
bitbake openssh

# 查看依賴
bitbake -g busybox

# 進入 devshell 除錯
bitbake -c devshell busybox

# 查看 recipe 變數
bitbake -e busybox | grep ^SRC_URI
```

**Recipe 範例：**

```bitbake
SUMMARY = "My application"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=..."

SRC_URI = "git://github.com/example/myapp.git;branch=main"
SRCREV = "abc123..."

S = "${WORKDIR}/git"

inherit cmake

do_install() {
    install -d ${D}${bindir}
    install -m 0755 myapp ${D}${bindir}/
}
```

---

## Q33. Buildroot 與 Yocto 的比較

### 回答

| 比較 | Buildroot | Yocto |
|---|---|---|
| **學習曲線** | 低（類似 Makefile） | 高（需學 BitBake/Recipe）|
| **構建速度** | 快 | 慢（但有 sstate cache）|
| **靈活性** | 中等 | 極高 |
| **包管理** | 無 runtime 包管理 | 可選 rpm/deb/ipk |
| **商業支援** | 社群為主 | Wind River、Mentor 等 |
| **適用** | 小型專案、快速原型 | 大型商業產品 |

---

## Q34. Cross Compilation 與工具鏈

### 回答

**Cross Compilation** = 在 A 架構的主機上，編譯出給 B 架構執行的程式。

**工具鏈命名格式：**

```
arch-vendor-os-libc-tool

arm-linux-gnueabihf-gcc
│    │       │        │
│    │       │        └── 工具（gcc/g++/ld/objdump...）
│    │       └────────── C library（gnueabihf = GNU glibc, hard float）
│    └────────────────── 作業系統
└─────────────────────── 目標架構
```

**常見工具鏈：**

```bash
# ARM 32-bit
arm-linux-gnueabihf-gcc

# ARM 64-bit (AArch64)
aarch64-linux-gnu-gcc

# RISC-V
riscv64-linux-gnu-gcc

# MIPS
mipsel-linux-gnu-gcc
```

**交叉編譯範例：**

```bash
# 設定工具鏈
export CROSS_COMPILE=arm-linux-gnueabihf-
export ARCH=arm

# 編譯 kernel
make defconfig
make -j8 zImage dtbs modules

# 編譯應用程式
${CROSS_COMPILE}gcc -o myapp myapp.c
${CROSS_COMPILE}strip myapp  # 移除 debug symbols，縮小體積
```

---

# 十一、除錯與效能分析

---

## Q35. Kernel Oops 與 Panic 如何分析？

### 回答

**Kernel Oops** = kernel 遇到非致命錯誤（如 NULL pointer dereference）

**Kernel Panic** = 致命錯誤，系統停止

**典型 Oops 訊息分析：**

```
BUG: unable to handle page fault for address: 0000000000000008
PGD 0 P4D 0
Oops: 0002 [#1] SMP PTI
CPU: 0 PID: 1234 Comm: myprocess
RIP: 0010:my_driver_func+0x1c/0x40  ← 出錯的函式 + offset
RSP: ffffc900...
RAX: 0000000000000000  ← RAX = NULL，然後存取 offset 8
...
Call Trace:                           ← 呼叫堆疊
 my_ioctl+0x45/0x80
 __x64_sys_ioctl+0x...
```

**分析工具：**

```bash
# 將 offset 轉換為程式碼行號
addr2line -e vmlinux my_driver_func+0x1c

# 反組譯查看出錯位置
objdump -d my_driver.ko | grep -A 20 "<my_driver_func>"

# decode_stacktrace.sh 自動解碼
./scripts/decode_stacktrace.sh vmlinux < oops.log
```

---

## Q36. 如何使用 `ftrace` 追蹤 Kernel 行為？

### 回答

`ftrace` 是 Linux 內建的 kernel tracer。

```bash
# 掛載 debugfs
mount -t debugfs nodev /sys/kernel/debug

cd /sys/kernel/debug/tracing

# 查看可用 tracer
cat available_tracers
# nop function function_graph irqsoff preemptoff ...

# 追蹤函式呼叫
echo function > current_tracer
echo my_driver_func > set_ftrace_filter
echo 1 > tracing_on
# ... 觸發操作 ...
echo 0 > tracing_on
cat trace

# 追蹤函式圖（含子呼叫與時間）
echo function_graph > current_tracer
echo my_driver_func > set_graph_function
cat trace

# 追蹤中斷關閉時間（找延遲問題）
echo irqsoff > current_tracer
```

---

## Q37. `printk` 日誌層級與使用建議

### 回答

```c
/* 8 個層級，數字越小越嚴重 */
KERN_EMERG   "0"  /* 系統無法使用 */
KERN_ALERT   "1"  /* 需要立即處理 */
KERN_CRIT    "2"  /* 嚴重錯誤 */
KERN_ERR     "3"  /* 一般錯誤 */
KERN_WARNING "4"  /* 警告 */
KERN_NOTICE  "5"  /* 正常但重要 */
KERN_INFO    "6"  /* 一般資訊 */
KERN_DEBUG   "7"  /* 除錯訊息 */

/* 現代建議用 pr_ 系列 */
pr_err("Failed to init: %d\n", ret);
pr_warn("Resource low\n");
pr_info("Driver loaded\n");
pr_debug("reg value = 0x%x\n", val);  /* 需 DEBUG 啟用 */

/* Device 相關用 dev_ 系列（自動加入 device 名稱）*/
dev_err(&pdev->dev, "probe failed: %d\n", ret);
dev_info(&pdev->dev, "initialized at 0x%llx\n", res->start);
```

**查看 kernel log：**

```bash
dmesg                    # 查看所有 kernel log
dmesg -T                 # 加上時間戳
dmesg --level=err,warn   # 只看錯誤和警告
cat /proc/kmsg           # 持續監看
journalctl -k            # systemd 系統查看 kernel log
```

---

## Q38. 如何使用 `perf` 進行效能分析？

### 回答

```bash
# 即時查看 CPU hotspot
perf top

# 統計各種硬體事件
perf stat ./myapp
perf stat -e cache-misses,cache-references,cycles ./myapp

# 記錄並分析（採樣）
perf record -g ./myapp       # -g 記錄 call graph
perf report                  # 互動式分析
perf report --stdio          # 文字輸出

# 追蹤系統呼叫
perf trace ./myapp

# 記錄特定事件
perf record -e sched:sched_switch -a sleep 5
perf report

# Cache miss 分析
perf stat -e L1-dcache-load-misses,L1-dcache-loads ./myapp
```

**解讀 `perf report` 輸出：**

```
Overhead  Symbol
   45.2%  [k] copy_to_user     ← kernel 函式，時間最多
   20.1%  [.] process_data     ← 使用者程式
    8.3%  [k] __memcpy
```

---

# 十二、電源管理

---

## Q39. Linux 電源管理框架（Runtime PM）

### 回答

**系統級電源管理：**

```
S0：工作狀態（Fully On）
S1：待機（CPU 停止，記憶體供電）
S2：類似 S1，CPU 斷電
S3：睡眠（Suspend-to-RAM）← 最常用
S4：休眠（Suspend-to-Disk）
S5：關機（Soft Off）
```

**Runtime PM（裝置級電源管理）：**

```c
/* Driver 啟用 Runtime PM */
pm_runtime_enable(&pdev->dev);
pm_runtime_set_active(&pdev->dev);

/* 使用前喚醒 */
pm_runtime_get_sync(&pdev->dev);
/* 使用後允許休眠 */
pm_runtime_put_sync(&pdev->dev);

/* 實作 suspend/resume callback */
static int my_runtime_suspend(struct device *dev)
{
    /* 關閉時鐘、降低電壓 */
    clk_disable_unprepare(priv->clk);
    regulator_disable(priv->vdd);
    return 0;
}

static int my_runtime_resume(struct device *dev)
{
    /* 恢復時鐘、電壓 */
    regulator_enable(priv->vdd);
    clk_prepare_enable(priv->clk);
    return 0;
}

static const struct dev_pm_ops my_pm_ops = {
    SET_RUNTIME_PM_OPS(my_runtime_suspend, my_runtime_resume, NULL)
    SET_SYSTEM_SLEEP_PM_OPS(my_suspend, my_resume)
};
```

---

## Q40. 什麼是 Clock Framework（clk）？

### 回答

Linux Common Clock Framework (CCF) 統一管理 SoC 上所有時鐘。

```
PLL（Phase-Locked Loop）
    |
Clock Divider
    |
Clock Mux（選擇來源）
    |
Gate（開關）
    |
周邊裝置（UART、SPI、USB...）
```

**Driver 使用：**

```c
/* 從 Device Tree 取得時鐘 */
struct clk *clk = devm_clk_get(&pdev->dev, "core");

/* 設定頻率 */
clk_set_rate(clk, 100000000);  /* 100MHz */

/* 啟用 */
clk_prepare_enable(clk);

/* 停用 */
clk_disable_unprepare(clk);

/* 查詢頻率 */
unsigned long rate = clk_get_rate(clk);
```

---

# 十三、安全機制

---

## Q41. ARM TrustZone 是什麼？

### 回答

TrustZone 是 ARM 處理器的硬體安全擴充，將系統分為兩個世界：

```
┌─────────────────┬─────────────────┐
│  Normal World   │  Secure World   │
│                 │                 │
│  Linux / RTOS   │  Trusted OS     │
│  普通應用程式    │  (OP-TEE 等)    │
│                 │                 │
│  NS=1          │  NS=0           │
└─────────────────┴─────────────────┘
          ↕ SMC（Secure Monitor Call）
         Monitor Mode（EL3）
```

**關鍵特性：**
- Normal World 完全無法存取 Secure World 的記憶體
- 即使 Linux kernel 被攻破，Secure World 的金鑰、指紋資料仍安全
- 透過 `SMC` 指令切換世界

**常見用途：**
- 儲存加密金鑰
- DRM 內容保護
- 指紋/臉部辨識
- Secure Boot 驗證

---

## Q42. Secure Boot 流程

### 回答

```
晶片燒錄公鑰 Hash（OTP/eFuse，不可修改）
        |
上電 → BootROM 用公鑰驗證 SPL 簽章
        |
    驗證成功 → 執行 SPL
    驗證失敗 → 拒絕啟動
        |
SPL 驗證 U-Boot 簽章
        |
U-Boot 驗證 Kernel 簽章（FIT Image）
        |
Kernel 驗證 rootfs（dm-verity）
        |
每一層都驗證下一層，形成信任鏈（Chain of Trust）
```

**Linux dm-verity（rootfs 完整性）：**

```bash
# 產生 hash tree
veritysetup format /dev/sda2 /dev/sda3

# 掛載並驗證
veritysetup open /dev/sda2 verified_root /dev/sda3 <root_hash>
mount /dev/mapper/verified_root /mnt
```

---

# 十四、C 語言與底層知識

---

## Q43. `volatile` 關鍵字在嵌入式中的重要性

### 回答

`volatile` 告訴編譯器：**不要對這個變數做最佳化，每次都從記憶體讀取。**

**為何重要：**

```c
/* 沒有 volatile：編譯器可能把 status 放在暫存器，只讀一次 */
uint32_t *status_reg = (uint32_t *)0x10000004;
while (*status_reg == 0);  /* 可能被最佳化成無限迴圈！ */

/* 正確：volatile 確保每次都讀記憶體 */
volatile uint32_t *status_reg = (volatile uint32_t *)0x10000004;
while (*status_reg == 0);  /* 每次迴圈都重新讀取 */

/* 中斷共享變數 */
volatile int data_ready = 0;

void isr(void) {
    data_ready = 1;  /* 中斷設置 */
}

void main_loop(void) {
    while (!data_ready);  /* 主迴圈等待 */
}
```

**volatile 不能取代 memory barrier：**
在多核系統上，還需要 `smp_rmb()` / `smp_wmb()` 確保記憶體存取順序。

---

## Q44. `static` 關鍵字在 Kernel 中的作用

### 回答

```c
/* 函式的 static：限制在本檔案可見（internal linkage）*/
static int my_internal_func(void)
{
    /* 外部無法直接呼叫此函式 */
    return 0;
}

/* 變數的 static：生命週期為整個模組載入期間 */
static int call_count = 0;  /* 不在 stack 上，持久存在 */

void increment(void) {
    call_count++;  /* 跨越函式呼叫保留值 */
}

/* 好處一：避免 symbol 污染 kernel global namespace */
/* 好處二：降低命名衝突（不同 driver 可以有同名 static 函式）*/
/* 好處三：編譯器可做 inlining 最佳化 */
```

---

## Q45. `container_of` 巨集詳解

### 回答

```c
/* 定義 */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* 使用情境 */
struct my_device {
    int id;
    struct list_head list;  /* 嵌入的 linked list 節點 */
    char name[32];
};

/* 假設我們只有 list_head 的指標 */
struct list_head *pos;
list_for_each(pos, &my_list) {
    /* 透過 list_head 指標回推到 my_device */
    struct my_device *dev = container_of(pos, struct my_device, list);
    pr_info("device id = %d\n", dev->id);
}
```

**運作原理：**

```
struct my_device 記憶體佈局：
┌──────────────────────────────┐ ← dev（結構體起始位址）
│  int id           (0..3)    │
├──────────────────────────────┤
│  struct list_head (4..11)   │ ← pos 指向這裡
├──────────────────────────────┤
│  char name[32]   (12..43)   │
└──────────────────────────────┘

container_of(pos, struct my_device, list)
= (struct my_device *)(pos - offsetof(struct my_device, list))
= (struct my_device *)(pos - 4)
= dev  ← 回到結構體起始位址
```

---

## Q46. Endianness（位元組序）問題

### 回答

```c
/* 大端（Big Endian）：高位在低位址 */
/* ARM, PowerPC 可設定，網路協定預設 Big Endian */

/* 小端（Little Endian）：低位在低位址 */
/* x86, ARM（預設）, RISC-V */

uint32_t val = 0x12345678;

/* Little Endian 記憶體 */
/* addr+0: 0x78, addr+1: 0x56, addr+2: 0x34, addr+3: 0x12 */

/* Big Endian 記憶體 */
/* addr+0: 0x12, addr+1: 0x34, addr+2: 0x56, addr+3: 0x78 */

/* 轉換函式（Linux kernel）*/
cpu_to_be32(val);  /* CPU 序 → Big Endian */
be32_to_cpu(val);  /* Big Endian → CPU 序 */
cpu_to_le32(val);  /* CPU 序 → Little Endian */
le32_to_cpu(val);  /* Little Endian → CPU 序 */

/* 網路程式（POSIX）*/
htonl(val);  /* host to network long (big endian) */
ntohl(val);  /* network to host long */
```

**實際問題：**
讀取硬體暫存器時，必須確認 SoC 和 driver 的 endianness 設定一致，否則讀到的值全是錯的。

---

## Q47. MMIO：Memory-Mapped I/O

### 回答

MMIO 讓 CPU 透過記憶體存取指令（load/store）來操作硬體暫存器。

```c
/* 第一步：映射實體位址到虛擬位址 */
void __iomem *base;
base = ioremap(0x10000000, 0x1000);  /* 實體位址, 大小 */

/* 或用 devm 版本 */
base = devm_ioremap_resource(&pdev->dev, res);

/* 讀取 32-bit 暫存器 */
uint32_t val = readl(base + REG_OFFSET);

/* 寫入 32-bit 暫存器 */
writel(0x1234, base + REG_OFFSET);

/* 讀寫 16-bit */
uint16_t val16 = readw(base + REG16_OFFSET);
writew(0xABCD, base + REG16_OFFSET);

/* 為何不能直接用 *(uint32_t *)ptr = val？*/
/* 答：編譯器可能重排序、快取問題、pipeline 問題 */
/* readl/writel 包含了必要的 memory barrier */

/* 解除映射 */
iounmap(base);
```

**MMIO 與 Port I/O 的差異（x86）：**

| | MMIO | Port I/O |
|---|---|---|
| 位址空間 | 與記憶體共用 | 獨立（64KB）|
| 指令 | MOV | IN/OUT |
| 現代嵌入式 | 主流 | 少見 |

---

## Q48. 記憶體屏障（Memory Barrier）

### 回答

在多核或有 out-of-order execution 的 CPU 上，編譯器和 CPU 可能重排記憶體存取順序。Memory Barrier 強制保證順序。

```c
/* 全序屏障（最強）*/
mb();   /* 讀寫都不能越過 */
rmb();  /* 讀屏障：之前的讀必須完成 */
wmb();  /* 寫屏障：之前的寫必須完成 */

/* SMP 屏障（多核專用）*/
smp_mb();
smp_rmb();
smp_wmb();

/* 典型使用場景：Ring Buffer */
/* Producer */
buffer[head] = data;
smp_wmb();  /* 確保資料寫入在更新 head 之前完成 */
head = (head + 1) % SIZE;

/* Consumer */
idx = tail;
smp_rmb();  /* 確保讀取 tail 後再讀資料 */
data = buffer[idx];

/* WRITE_ONCE / READ_ONCE：防止編譯器最佳化 */
WRITE_ONCE(shared_var, value);
value = READ_ONCE(shared_var);
```

---

# 附錄：面試快速複習清單

## 高頻考題清單

### 記憶體類
- [ ] mmap 原理（COW、Demand Paging）
- [ ] kmalloc vs vmalloc vs kzalloc
- [ ] GFP_KERNEL vs GFP_ATOMIC
- [ ] Virtual vs Physical address，MMU 運作
- [ ] TLB 是什麼，HugePages 好處

### 行程類
- [ ] fork() 回傳值，COW 機制
- [ ] Zombie process 原因與解法
- [ ] CFS 排程器原理（vruntime、Red-Black Tree）
- [ ] Context switch 成本

### 驅動類
- [ ] 字元裝置完整註冊流程
- [ ] ioctl magic number
- [ ] platform driver / device tree matching
- [ ] devm_* 好處
- [ ] copy_from_user 為何需要

### 中斷類
- [ ] Top half / Bottom half（softirq/tasklet/workqueue 差異）
- [ ] spinlock vs mutex
- [ ] GFP_ATOMIC 使用場景

### 通訊類
- [ ] I2C 特性（兩線、位址定址、速度）
- [ ] SPI 特性（四線、全雙工、CPOL/CPHA）
- [ ] UART 8N1 意義
- [ ] GPIO interrupt 設定

### Boot 類
- [ ] BootROM → SPL → U-Boot → Kernel 流程
- [ ] bootargs 參數意義
- [ ] Device Tree 解決什麼問題

### 即時系統類
- [ ] RTOS vs Linux 差異
- [ ] Priority Inversion 與 Priority Inheritance
- [ ] FreeRTOS 核心元件

### 工具鏈類
- [ ] Yocto Layer / Recipe 概念
- [ ] Cross compile 工具鏈命名規則
- [ ] CROSS_COMPILE / ARCH 環境變數

### C 語言類
- [ ] volatile 用途
- [ ] static 在 kernel 的意義
- [ ] container_of 原理
- [ ] Memory barrier 用途

---

## 常見追問問題

**Q: 你說 kzalloc 比較安全，那效能呢？**
A: memset 有額外開銷，但現代 CPU 的 memset 非常快，對 4KB 以下的配置影響微乎其微。Driver 初始化路徑通常不是效能瓶頸，安全性優先，使用 kzalloc。

**Q: 中斷下半部三種方式你會怎麼選？**
A: 優先考慮 workqueue（因為可睡眠、最靈活）。若不需要睡眠且使用頻率低，用 tasklet。只有像網路協定棧這種極高頻且有並行需求的才用 softirq（因為需要多 CPU 並行且不能睡眠）。

**Q: 有沒有用過 JTAG 或其他 debug 工具？**
A: JTAG 可以在 CPU 暫停時直接存取暫存器和記憶體，連 boot 早期問題都能抓。搭配 OpenOCD + GDB 可以設斷點、單步追蹤 kernel。

---

> 本文件版本：v2.0
> 內容涵蓋：Linux Kernel、Driver、Memory、Process、RTOS、通訊協定、Build System、除錯工具
