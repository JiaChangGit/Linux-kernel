# QEMU Platform Device Driver API 技術分析報告

本報告只根據 `/qemu-platform-demo` 目前實際存在的檔案進行分析，主要依據為 source code，其次為 header、build system、script、DTS 與 README/comment。原報告已涵蓋 project structure、semantic elements、API inventory、call graph、resource tracing 與 architecture report；本次更新保留這個分析順序，補上可直接追溯的 execution flow、callback chain、ownership/lifecycle 與風險分析。

以下內容分成：

- `# Direct Observation`：可從目前程式碼、DTS、Makefile 或 scripts 直接驗證。
- `# Conservative Inference`：只基於已存在的呼叫關係做保守推論，會明確標示。
- 若未能從 codebase 驗證，會標示「目前程式碼中未觀察到」或「無法從現有內容確認」。

---

## 第一階段：Codebase Trace

### 1. Project Structure

#### # Direct Observation

`qemu-platform-demo` 的可驗證結構如下：

| 類別 | 檔案 | 角色 |
|---|---|---|
| source file | `driver/myled_ctrl.c` | Linux platform driver 主體，包含 register helper、hardware init/shutdown、sysfs attributes、probe/remove、PM callback、OF match table 與 module registration。 |
| header file | `driver/myled_ctrl.h` | 定義 register offset、bit mask、限制值、hardware version、simulated register count 與 `struct myled_priv`。 |
| build system | `driver/Makefile` | out-of-tree kernel module build，`obj-m := myled_ctrl.o`，並提供 `install` target 複製 `.ko` 到 `rootfs/overlay/`。 |
| hardware description | `dts/myled-fragment.dts` | DT overlay fragment，建立 `myled-controller@0d000000` node，宣告 `compatible`、`reg`、`num-leds`、`label`、`default-brightness`、`status`。 |
| DTB script | `dts/patch_dtb.sh`、`scripts/02_patch_dtb.sh` | dump QEMU virt base DTB，編譯 overlay DTBO，再用 `fdtoverlay` 合併成 `qemu-virt-myled.dtb`。 |
| build/run scripts | `scripts/00_install_deps.sh`、`01_build_kernel.sh`、`03_build_driver.sh`、`04_build_rootfs.sh`、`05_run_qemu.sh`、`06_clean.sh` | 安裝依賴、下載/建置 kernel、建置 module、打包 initramfs、啟動 QEMU、清理產物。 |
| rootfs overlay | `rootfs/overlay/init` | QEMU initramfs 的 `/init`，mount proc/sys/dev，`insmod /myled_ctrl.ko`，執行 `/test_myled.sh`，最後 drop to shell。 |
| rootfs test | `rootfs/overlay/test_myled.sh` | 從 `/sys/bus/platform/devices` 找到包含 `0d000000` 的 device，讀寫 `myled` sysfs attribute。 |
| docs | `docs/*.png` | build/demo 截圖。此報告未以圖片內容推導 driver 行為。 |
| README/report | `README_platform.md`、`report_platform.md` | 說明性文件，僅作低優先級佐證。 |

#### Module / Component Relationship

```text
scripts/01_build_kernel.sh
  -> linux-6.6.30/arch/arm64/boot/Image

scripts/02_patch_dtb.sh
  -> dts/patch_dtb.sh
  -> dts/myled-fragment.dts
  -> dts/qemu-virt-myled.dtb

scripts/03_build_driver.sh
  -> driver/Makefile
  -> driver/myled_ctrl.c + driver/myled_ctrl.h
  -> rootfs/overlay/myled_ctrl.ko

scripts/04_build_rootfs.sh
  -> rootfs/overlay/init
  -> rootfs/overlay/test_myled.sh
  -> rootfs/overlay/myled_ctrl.ko
  -> rootfs/initramfs.cpio.gz

scripts/05_run_qemu.sh
  -> qemu-system-aarch64 -kernel Image -dtb qemu-virt-myled.dtb -initrd initramfs.cpio.gz
  -> /init
  -> insmod /myled_ctrl.ko
  -> platform driver bind
  -> sysfs /sys/bus/platform/devices/<device>/myled/
```

---

### 2. Semantic Element Extraction

#### # Direct Observation

以下只列目前實際存在的元素。

| 類型 | 名稱 | 定義位置 | 說明 |
|---|---|---|---|
| API / registration macro | `module_platform_driver(myled_driver)` | `driver/myled_ctrl.c:440` | 產生 module init/exit glue，註冊 `struct platform_driver`。 |
| dispatch table / operation table | `static struct platform_driver myled_driver` | `driver/myled_ctrl.c:429` | `.probe = myled_probe`、`.remove = myled_remove`、`.driver.of_match_table = myled_of_match`、`.driver.pm = &myled_pm_ops`。 |
| callback | `myled_probe` | `driver/myled_ctrl.c:303` | platform device match 後的初始化 callback。 |
| callback | `myled_remove` | `driver/myled_ctrl.c:380` | driver unbind/module remove 時的 cleanup callback。 |
| callback table | `myled_pm_ops` | `driver/myled_ctrl.c:412` | 綁定 system sleep suspend/resume callbacks。 |
| callback | `myled_suspend` | `driver/myled_ctrl.c:398` | system suspend 時清除 `MYLED_CTRL_ENABLE`。 |
| callback | `myled_resume` | `driver/myled_ctrl.c:405` | system resume 時設回 `MYLED_CTRL_ENABLE`。 |
| OF match table | `myled_of_match` | `driver/myled_ctrl.c:419` | 支援 `"myvendor,myled-v1"` 與 `"myvendor,myled"`。 |
| module alias annotation | `MODULE_DEVICE_TABLE(of, myled_of_match)` | `driver/myled_ctrl.c:423` | 將 OF match table 匯出到 module device table。 |
| sysfs attribute macro | `DEVICE_ATTR_RW(enable)` | `driver/myled_ctrl.c:176` | 建立 read/write `enable` attribute。 |
| sysfs attribute macro | `DEVICE_ATTR_RW(brightness)` | `driver/myled_ctrl.c:201` | 建立 read/write `brightness` attribute。 |
| sysfs attribute macro | `DEVICE_ATTR_RW(color)` | `driver/myled_ctrl.c:224` | 建立 read/write `color` attribute。 |
| sysfs attribute macro | `DEVICE_ATTR_RW(blink)` | `driver/myled_ctrl.c:250` | 建立 read/write `blink` attribute。 |
| sysfs attribute macro | `DEVICE_ATTR_RO(status)` | `driver/myled_ctrl.c:260` | 建立 read-only `status` attribute。 |
| sysfs attribute macro | `DEVICE_ATTR_RO(info)` | `driver/myled_ctrl.c:282` | 建立 read-only `info` attribute。 |
| dispatch table | `myled_attrs[]` | `driver/myled_ctrl.c:284` | sysfs attribute list，包含 enable、brightness、color、blink、status、info。 |
| dispatch group | `myled_attr_group` | `driver/myled_ctrl.c:294` | `.name = "myled"`，所以 attribute 會在 device kobject 下的 `myled/` 子目錄。 |
| register helper | `myled_reg_read` | `driver/myled_ctrl.c:35` | 在 spinlock 保護下讀取 `sim_regs` 或 MMIO `readl`。 |
| register helper | `myled_reg_write` | `driver/myled_ctrl.c:49` | 在 spinlock 保護下寫入 `sim_regs` 或 MMIO `writel`。 |
| register helper | `myled_reg_set_bits` | `driver/myled_ctrl.c:60` | read-modify-write 設定位元。 |
| register helper | `myled_reg_clr_bits` | `driver/myled_ctrl.c:65` | read-modify-write 清除位元。 |
| lifecycle helper | `myled_hw_init` | `driver/myled_ctrl.c:108` | 讀 VERSION、必要時切 simulated mode、設定 default brightness、enable controller。 |
| lifecycle helper | `myled_hw_shutdown` | `driver/myled_ctrl.c:139` | 清除 enable/blink/pwm bit，brightness 寫 0。 |
| memory management | `devm_kzalloc` | 呼叫於 `driver/myled_ctrl.c:312` | 分配 `struct myled_priv`，生命週期綁定 `struct device`。 |
| resource mapping | `platform_get_resource` | 呼叫於 `driver/myled_ctrl.c:332` | 取得 DT `reg` 轉換後的 MEM resource。 |
| resource mapping | `devm_ioremap_resource` | 呼叫於 `driver/myled_ctrl.c:340` | request/map MEM resource；失敗時 driver 進入 simulated mode。 |
| state binding | `platform_set_drvdata` / `dev_set_drvdata` | `driver/myled_ctrl.c:353-354` | 將 `priv` 綁到 platform device/device，供 sysfs 與 PM callbacks 取回。 |
| synchronization primitive | `spinlock_t lock` | `driver/myled_ctrl.h:45` | 保護 register access path。 |
| compiler annotation | `void __iomem *base` | `driver/myled_ctrl.h:40` | 標示 MMIO base pointer。 |
| compiler annotation | `__maybe_unused` | `driver/myled_ctrl.c:398`、`405` | 用於 PM callbacks，避免某些 config 下 unused warning。 |
| conditional compilation macro | `SET_SYSTEM_SLEEP_PM_OPS` | `driver/myled_ctrl.c:413` | 依 kernel PM config 展開 system sleep callbacks。 |
| external interface | sysfs files under `.../myled/` | `driver/myled_ctrl.c:284-296` | user space 透過 rootfs test script 讀寫。 |
| external interface | Device Tree node | `dts/myled-fragment.dts:26-32` | 透過 `compatible` 觸發 OF matching，透過 `reg` 與 custom properties 餵給 probe/init。 |

#### 目前程式碼中未觀察到

- IRQ registration，例如 `request_irq`、`devm_request_irq`。
- workqueue、tasklet、timer 或 threaded IRQ。
- wait queue、completion、mutex、atomic API。
- char device、miscdevice、ioctl、netlink、debugfs。
- runtime PM callbacks，例如 `.runtime_suspend` 或 `.runtime_resume`；目前只有 `pm_runtime_enable/disable` 與 system sleep PM ops。
- DMA、buffer ownership transfer、userspace mmap。

---

### 3. API / Macro Inventory

#### Initialization

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| `module_platform_driver(myled_driver)` | macro | `driver/myled_ctrl.c:440` | module load path；rootfs `init` 執行 `insmod /myled_ctrl.ko` 於 `rootfs/overlay/init:16` | 註冊 platform driver | `myled_driver` | 讓 kernel platform bus 可以用 OF table match device 並呼叫 `myled_probe`。 |
| `myled_driver` | dispatch table | `driver/myled_ctrl.c:429` | `module_platform_driver` | 提供 probe/remove/driver metadata | `myled_probe`、`myled_remove`、`myled_of_match`、`myled_pm_ops` | 將 Linux driver core 的事件 dispatch 到本 driver callback。 |
| `myled_of_match` | match table | `driver/myled_ctrl.c:419` | driver core matching；DTS compatible 於 `dts/myled-fragment.dts:27` | OF compatible matching | `"myvendor,myled-v1"`、`"myvendor,myled"` | DTS node compatible match 後，probe 才會被呼叫。 |
| `MODULE_DEVICE_TABLE(of, myled_of_match)` | module annotation | `driver/myled_ctrl.c:423` | build/module metadata | 匯出 OF alias | `myled_of_match` | 支援 module device table metadata；是否自動載入需依 userspace/module loader，現有 rootfs 是手動 `insmod`。 |
| `myled_probe` | callback | `driver/myled_ctrl.c:303` | platform bus match 後呼叫 | 分配 private data、解析 DT、map MMIO、init HW、建立 sysfs、enable PM runtime | `struct platform_device`、`struct myled_priv`、`myled_attr_group` | 是 driver 的主要 entry point。 |
| `devm_kzalloc` | managed allocation API | 呼叫於 `driver/myled_ctrl.c:312` | `myled_probe` | 分配並清零 `priv` | `struct myled_priv` | 若失敗回傳 `-ENOMEM`，probe 中止。 |
| `spin_lock_init` | synchronization init | 呼叫於 `driver/myled_ctrl.c:317` | `myled_probe` | 初始化 `priv->lock` | `spinlock_t lock` | register helper 可安全進入 lock/unlock path。 |
| `of_property_read_u32` | OF property API | 呼叫於 `driver/myled_ctrl.c:320`、`123` | `myled_probe`、`myled_hw_init` | 讀 `num-leds` 與 `default-brightness` | DT node | `num-leds` 缺失時 fallback 1；`default-brightness` 缺失時 fallback 128。 |
| `of_property_read_string` | OF property API | 呼叫於 `driver/myled_ctrl.c:328` | `myled_probe` | 讀 `label` 並印 log | DT node | 不影響 state；只作 log。 |
| `platform_get_resource` | platform resource API | 呼叫於 `driver/myled_ctrl.c:332` | `myled_probe` | 取得 MEM resource | `reg = <0x0 0x0d000000 0x0 0x1000>` | 若無 resource，設定 `priv->simulated = true`。 |
| `devm_ioremap_resource` | managed MMIO API | 呼叫於 `driver/myled_ctrl.c:340` | `myled_probe` | map MMIO resource | `priv->base` | 失敗時清 `base` 並進入 simulated mode，而非 probe fail。 |
| `platform_set_drvdata` / `dev_set_drvdata` | state binding API | 呼叫於 `driver/myled_ctrl.c:353-354` | `myled_probe` | 綁定 `priv` | `pdev`、`dev`、`priv` | sysfs show/store 與 PM callbacks 能透過 `dev_get_drvdata` / `platform_get_drvdata` 找回 state。 |
| `myled_hw_init` | lifecycle helper | `driver/myled_ctrl.c:108` | `myled_probe` | VERSION check、sim fallback、brightness init、enable controller | `priv->simulated`、`sim_regs`、register macros | 成功後 device 進入可操作狀態；目前實作固定 return 0。 |
| `sysfs_create_group` | sysfs registration API | 呼叫於 `driver/myled_ctrl.c:364` | `myled_probe` | 建立 `myled/` attribute group | `myled_attr_group` | 成功後 userspace runtime path 可讀寫 sysfs；失敗會呼叫 `myled_hw_shutdown` 並回傳錯誤。 |
| `pm_runtime_enable` | PM API | 呼叫於 `driver/myled_ctrl.c:372` | `myled_probe` | 啟用 runtime PM framework state | `struct device` | 目前程式碼未註冊 runtime PM callbacks，因此只看到 enable/disable 動作。 |

#### Registration / External Interface

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| `DEVICE_ATTR_RW(enable)` | macro | `driver/myled_ctrl.c:176` | `myled_attrs[]` | 產生 enable attribute | `enable_show`、`enable_store` | 使用者讀寫 `enable` 會 dispatch 到對應 callback。 |
| `DEVICE_ATTR_RW(brightness)` | macro | `driver/myled_ctrl.c:201` | `myled_attrs[]` | 產生 brightness attribute | `brightness_show`、`brightness_store` | 使用者可讀寫 brightness register。 |
| `DEVICE_ATTR_RW(color)` | macro | `driver/myled_ctrl.c:224` | `myled_attrs[]` | 產生 color attribute | `color_show`、`color_store` | 使用者可讀寫 color register。 |
| `DEVICE_ATTR_RW(blink)` | macro | `driver/myled_ctrl.c:250` | `myled_attrs[]` | 產生 blink attribute | `blink_show`、`blink_store` | 使用者可切換 CTRL blink bit。 |
| `DEVICE_ATTR_RO(status)` | macro | `driver/myled_ctrl.c:260` | `myled_attrs[]` | 產生 status read-only attribute | `status_show` | 使用者只能讀 status flags。 |
| `DEVICE_ATTR_RO(info)` | macro | `driver/myled_ctrl.c:282` | `myled_attrs[]` | 產生 info read-only attribute | `info_show` | 輸出 version、num_leds、simulated、ctrl、brightness、color。 |
| `myled_attrs[]` | dispatch table | `driver/myled_ctrl.c:284` | `myled_attr_group` | 聚合所有 attributes | `dev_attr_*` | 決定 sysfs group 暴露哪些 files。 |
| `myled_attr_group` | sysfs group | `driver/myled_ctrl.c:294` | `sysfs_create_group` / `sysfs_remove_group` | 建立 named group `.name = "myled"` | `myled_attrs` | 決定 sysfs path 會是 device kobject 底下的 `myled/`。 |

#### Execution Path / Register Access

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| `myled_reg_read` | helper | `driver/myled_ctrl.c:35` | show callbacks、`myled_hw_init`、bit helpers、`info_show` | 依 `priv->simulated` 選擇讀 `sim_regs[off/4]` 或 `readl(base + off)` | `priv->lock`、`priv->base`、`priv->sim_regs` | 統一 read path，並以 spinlock 保護。 |
| `myled_reg_write` | helper | `driver/myled_ctrl.c:49` | store callbacks、`myled_hw_init`、`myled_hw_shutdown`、bit helpers | 依 `priv->simulated` 選擇寫 shadow array 或 MMIO | 同上 | 統一 write path。 |
| `myled_reg_set_bits` | helper | `driver/myled_ctrl.c:60` | `myled_hw_init`、`enable_store`、`blink_store`、`myled_resume` | read-modify-write 設定 bits | `MYLED_REG_CTRL`、mask | 不是單一 lock 覆蓋整個 RMW；讀與寫各自 lock。 |
| `myled_reg_clr_bits` | helper | `driver/myled_ctrl.c:65` | `myled_hw_shutdown`、`enable_store`、`blink_store`、`myled_suspend` | read-modify-write 清除 bits | `MYLED_REG_CTRL`、mask | 同上，RMW 期間可能被其他 writer 交錯。 |

#### Lifecycle / Cleanup

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 呼叫來源 | 用途 | 關聯 struct/data | 對 execution flow 的影響 |
|---|---|---|---|---|---|---|
| `myled_hw_shutdown` | lifecycle helper | `driver/myled_ctrl.c:139` | `myled_remove`、`sysfs_create_group` 失敗 path | 清除 enable/blink/pwm 並 brightness 寫 0 | CTRL / BRIGHTNESS registers | 將 controller 關閉，作為 cleanup 的 device state 收尾。 |
| `myled_remove` | callback | `driver/myled_ctrl.c:380` | platform driver remove/unbind | disable runtime PM、移除 sysfs group、shutdown HW | `priv` from `platform_get_drvdata` | cleanup chain 的主要入口。 |
| `sysfs_remove_group` | cleanup API | 呼叫於 `driver/myled_ctrl.c:387` | `myled_remove` | 移除 `myled/` sysfs files | `myled_attr_group` | 防止 remove 後 userspace 繼續透過 sysfs 進入 callbacks。 |
| `pm_runtime_disable` | PM cleanup API | 呼叫於 `driver/myled_ctrl.c:386` | `myled_remove` | 關閉 runtime PM | `struct device` | 與 probe 的 `pm_runtime_enable` 對應。 |

#### Logging / Debug / Error Handling

| 名稱 | 類型 | 定義位置 | 呼叫位置 / 呼叫來源 | 用途 | 對 execution flow 的影響 |
|---|---|---|---|---|---|
| `dev_info` | logging API | 多處 | probe/init/remove/sysfs success path | 記錄 probe、MMIO region、simulated mode、init/remove 狀態 | 不改變 flow。 |
| `dev_warn` | logging API | `driver/myled_ctrl.c:113`、`321`、`333`、`342` | version mismatch、DT property 缺失、MEM resource 缺失、ioremap 失敗 | 記錄 non-fatal fallback | 對 `num-leds`、resource mapping、simulated mode 有 fallback 行為。 |
| `dev_err` | logging API | `driver/myled_ctrl.c:359`、`366` | `myled_hw_init` 或 `sysfs_create_group` 失敗 | 記錄 fatal probe error | `myled_hw_init` 目前不會回傳錯誤；`sysfs_create_group` 失敗會中止 probe。 |
| `kstrtobool` | parsing API | `enable_store`、`blink_store` | sysfs write | parse bool | parse 失敗回傳錯誤，不改 register。 |
| `kstrtou32` | parsing API | `brightness_store`、`color_store` | sysfs write | parse decimal/hex u32 | parse 失敗回傳錯誤，不改 register。 |
| `-EINVAL` | error code | `brightness_store` | sysfs write brightness | brightness > 255 時拒絕 | 防止超過 `MYLED_MAX_BRIGHTNESS`。 |

---

### 4. Call Graph

#### Initialization Chain

```text
scripts/05_run_qemu.sh
  -> qemu-system-aarch64 -machine virt -kernel Image -dtb dts/qemu-virt-myled.dtb -initrd rootfs/initramfs.cpio.gz
  -> kernel uses DTB containing myled-controller@0d000000
  -> /init from initramfs
  -> insmod /myled_ctrl.ko
  -> module_platform_driver(myled_driver)
  -> platform_driver_register(&myled_driver)
  -> platform bus / OF match
       myled_of_match[]:
         "myvendor,myled-v1"
         "myvendor,myled"
       DTS:
         compatible = "myvendor,myled-v1"
  -> myled_probe(pdev)
       -> devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL)
       -> priv->dev = dev
       -> spin_lock_init(&priv->lock)
       -> of_property_read_u32(dev->of_node, "num-leds", &priv->num_leds)
       -> of_property_read_string(dev->of_node, "label", &label)
       -> platform_get_resource(pdev, IORESOURCE_MEM, 0)
       -> devm_ioremap_resource(dev, res) or priv->simulated = true
       -> platform_set_drvdata(pdev, priv)
       -> dev_set_drvdata(dev, priv)
       -> myled_hw_init(priv)
       -> sysfs_create_group(&dev->kobj, &myled_attr_group)
       -> pm_runtime_enable(dev)
```

#### Runtime Chain: sysfs Read

```text
cat /sys/bus/platform/devices/<DEV>/myled/brightness
  -> sysfs dispatch via dev_attr_brightness
  -> brightness_show(dev, attr, buf)
  -> dev_get_drvdata(dev)
  -> myled_reg_read(priv, MYLED_REG_BRIGHTNESS)
       -> spin_lock_irqsave(&priv->lock, flags)
       -> if priv->simulated:
            val = priv->sim_regs[MYLED_REG_BRIGHTNESS / 4]
          else:
            val = readl(priv->base + MYLED_REG_BRIGHTNESS)
       -> spin_unlock_irqrestore(&priv->lock, flags)
  -> sysfs_emit(buf, "%u\n", val)
```

#### Runtime Chain: sysfs Write

```text
echo 200 > /sys/bus/platform/devices/<DEV>/myled/brightness
  -> sysfs dispatch via dev_attr_brightness
  -> brightness_store(dev, attr, buf, count)
  -> dev_get_drvdata(dev)
  -> kstrtou32(buf, 10, &val)
  -> if val > MYLED_MAX_BRIGHTNESS: return -EINVAL
  -> myled_reg_write(priv, MYLED_REG_BRIGHTNESS, val)
       -> spin_lock_irqsave(&priv->lock, flags)
       -> if priv->simulated:
            priv->sim_regs[MYLED_REG_BRIGHTNESS / 4] = val
          else:
            writel(val, priv->base + MYLED_REG_BRIGHTNESS)
       -> spin_unlock_irqrestore(&priv->lock, flags)
  -> return count
```

#### Cleanup Chain

```text
module unload / driver unbind
  -> platform driver core
  -> myled_remove(pdev)
       -> dev = &pdev->dev
       -> priv = platform_get_drvdata(pdev)
       -> pm_runtime_disable(dev)
       -> sysfs_remove_group(&dev->kobj, &myled_attr_group)
       -> myled_hw_shutdown(priv)
            -> myled_reg_clr_bits(priv, MYLED_REG_CTRL,
                                  MYLED_CTRL_ENABLE | MYLED_CTRL_BLINK | MYLED_CTRL_PWM_AUTO)
            -> myled_reg_write(priv, MYLED_REG_BRIGHTNESS, 0)
       -> devm resources released by driver core after device teardown
```

#### Callback Chain

```text
OF/platform callback:
  myled_driver.probe  -> myled_probe
  myled_driver.remove -> myled_remove

PM callback:
  myled_driver.driver.pm -> myled_pm_ops
  SET_SYSTEM_SLEEP_PM_OPS(myled_suspend, myled_resume)
  system suspend -> myled_suspend -> myled_reg_clr_bits(... ENABLE)
  system resume  -> myled_resume  -> myled_reg_set_bits(... ENABLE)

sysfs callback:
  myled_attr_group.attrs -> dev_attr_enable.attr      -> enable_show / enable_store
                          -> dev_attr_brightness.attr -> brightness_show / brightness_store
                          -> dev_attr_color.attr      -> color_show / color_store
                          -> dev_attr_blink.attr      -> blink_show / blink_store
                          -> dev_attr_status.attr     -> status_show
                          -> dev_attr_info.attr       -> info_show
```

#### Indirect Call Chain / Dispatch Table

| Dispatch point | Table / function pointer | Target | Evidence |
|---|---|---|---|
| platform driver registration | `myled_driver.probe` | `myled_probe` | `driver/myled_ctrl.c:429-430` |
| platform driver removal | `myled_driver.remove` | `myled_remove` | `driver/myled_ctrl.c:429-431` |
| OF matching | `myled_driver.driver.of_match_table` | `myled_of_match` | `driver/myled_ctrl.c:435` |
| PM system sleep | `myled_driver.driver.pm` | `myled_pm_ops` | `driver/myled_ctrl.c:436` |
| PM operation table | `SET_SYSTEM_SLEEP_PM_OPS` | `myled_suspend` / `myled_resume` | `driver/myled_ctrl.c:412-413` |
| sysfs group | `myled_attr_group.attrs` | `myled_attrs[]` | `driver/myled_ctrl.c:294-296` |
| sysfs attribute | `DEVICE_ATTR_RW/RO` generated callbacks | `*_show` / `*_store` | `driver/myled_ctrl.c:176`、`201`、`224`、`250`、`260`、`282` |

---

### 5. Struct / Resource Tracing

#### `struct myled_priv`

##### # Direct Observation

定義於 `driver/myled_ctrl.h:39-46`：

```c
struct myled_priv {
  void __iomem* base;
  struct device* dev;
  u32 num_leds;
  bool simulated;
  u32 sim_regs[MYLED_SIM_REG_COUNT];
  spinlock_t lock;
};
```

| 欄位 | 初始化 / 寫入位置 | 使用位置 | ownership / lifetime |
|---|---|---|---|
| `base` | `devm_ioremap_resource` 成功時寫入；失敗時設 `NULL` (`driver/myled_ctrl.c:340-344`) | `myled_reg_read/write` 非 simulated path | MMIO mapping 使用 devm 管理，lifetime 綁定 device。 |
| `dev` | `priv->dev = dev` (`driver/myled_ctrl.c:316`) | `myled_hw_init`、`myled_hw_shutdown` logging 與 OF property read | back pointer，不擁有 `struct device`。 |
| `num_leds` | `of_property_read_u32`，失敗 fallback 1 (`driver/myled_ctrl.c:320-324`) | `myled_hw_init` log、`info_show` output | value state，跟 `priv` 一起存在。 |
| `simulated` | MEM resource 缺失、ioremap 失敗、VERSION mismatch 時設 true (`driver/myled_ctrl.c:333`、`344`、`116`) | register helpers、`info_show` | 決定 register backend。 |
| `sim_regs[]` | `devm_kzalloc` 清零；simulated fallback 時寫 VERSION/STATUS (`driver/myled_ctrl.c:117-118`)；sysfs/register helpers 讀寫 | `myled_reg_read/write` simulated path | `priv` 內嵌 shadow register bank，不另行分配或釋放。 |
| `lock` | `spin_lock_init` (`driver/myled_ctrl.c:317`) | `myled_reg_read/write` | 保護 register access，生命週期跟 `priv` 一致。 |

#### Allocation / Init

```text
myled_probe
  -> devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL)
  -> priv->dev = dev
  -> spin_lock_init(&priv->lock)
  -> parse DT into priv->num_leds
  -> map resource into priv->base or set priv->simulated
  -> platform_set_drvdata / dev_set_drvdata
  -> myled_hw_init
```

#### Ownership

- `priv`：由 `devm_kzalloc` 分配，driver 不手動 `kfree`。ownership 綁在 `struct device` 的 managed resource。
- `base`：由 `devm_ioremap_resource` 建立 mapping，driver 不手動 `iounmap`。
- `sim_regs`：內嵌於 `priv`，不存在獨立 ownership transfer。
- sysfs group：由 `sysfs_create_group` 建立，必須由 `sysfs_remove_group` 移除；程式碼於 `myled_remove` 有對應移除。
- module file：`scripts/03_build_driver.sh` 透過 `make install` 複製到 `rootfs/overlay/`，`scripts/04_build_rootfs.sh` 再放進 initramfs。這是 build artifact flow，不是 kernel runtime ownership。

#### Lifetime / State Transition

```text
allocated zeroed priv
  -> lock initialized
  -> DT state loaded: num_leds
  -> resource state:
       base mapped, simulated=false
       or base=NULL, simulated=true
  -> drvdata bound
  -> hardware state:
       read VERSION
       if mismatch: simulated=true, seed VERSION/STATUS
       write brightness
       set CTRL_ENABLE
  -> sysfs visible
  -> runtime sysfs reads/writes mutate CTRL/BRIGHTNESS/COLOR or read STATUS/VERSION
  -> remove:
       runtime PM disabled
       sysfs hidden
       CTRL enable/blink/pwm cleared
       brightness zeroed
  -> devm cleanup after device/driver teardown
```

#### Data Passing Path

```text
DTS default-brightness
  -> of_property_read_u32(priv->dev->of_node, "default-brightness", &brightness)
  -> min(brightness, MYLED_MAX_BRIGHTNESS)
  -> myled_reg_write(priv, MYLED_REG_BRIGHTNESS, brightness)
  -> visible through brightness_show / info_show

User echo brightness
  -> brightness_store
  -> kstrtou32
  -> range check <= MYLED_MAX_BRIGHTNESS
  -> myled_reg_write
  -> subsequent brightness_show sees new value
```

#### Callback Binding

- `platform_set_drvdata(pdev, priv)` 讓 `myled_remove` 使用 `platform_get_drvdata(pdev)` 取得同一個 `priv`。
- `dev_set_drvdata(dev, priv)` 讓 sysfs show/store 與 PM callbacks 使用 `dev_get_drvdata(dev)` 取得同一個 `priv`。
- 這兩個 binding 在 `myled_hw_init` 與 `sysfs_create_group` 前完成；source comment 也指出 sysfs callbacks 需要它。

---

### 6. Execution Trace

#### Initialization Flow

```text
[Build]
01_build_kernel.sh
  -> download linux-6.6.30
  -> make defconfig
  -> make Image modules

02_patch_dtb.sh
  -> dts/patch_dtb.sh
  -> qemu-system-aarch64 -machine virt,dumpdtb=qemu-virt-base.dtb
  -> dtc -I dts -O dtb -@ myled-fragment.dts
  -> fdtoverlay base + myled-fragment.dtbo
  -> qemu-virt-myled.dtb

03_build_driver.sh
  -> make -C KDIR M=driver ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
  -> make install
  -> rootfs/overlay/myled_ctrl.ko

04_build_rootfs.sh
  -> copy BusyBox, init, test_myled.sh, myled_ctrl.ko
  -> pack rootfs/initramfs.cpio.gz

[Boot]
05_run_qemu.sh
  -> QEMU boots Image + qemu-virt-myled.dtb + initramfs
  -> rdinit=/init
  -> /init mounts proc/sys/dev
  -> insmod /myled_ctrl.ko
  -> platform driver registration
  -> OF compatible match
  -> myled_probe
  -> sysfs group available
```

#### Runtime Flow

```text
/init
  -> sh /test_myled.sh
  -> DEV=$(ls /sys/bus/platform/devices | grep "0d000000" | sed -n '1p')
  -> MYLED=/sys/bus/platform/devices/${DEV}/myled
  -> read info enable brightness color blink status
  -> echo 200 > brightness
  -> echo ff3300 > color
  -> echo 1 > blink
  -> echo 0 > enable
  -> cat info
  -> dmesg | grep -i myled
```

#### Cleanup Flow

```text
driver remove/unbind/module unload
  -> myled_remove
  -> pm_runtime_disable
  -> sysfs_remove_group
  -> myled_hw_shutdown
  -> devm cleanup later

script cleanup
  -> scripts/06_clean.sh
  -> removes driver build artifacts, DTB artifacts, rootfs artifacts
  -> optionally runs mrproper in linux-* / removes kernel source with --all
```

#### Event Flow

```text
Device Tree event:
  DT node status="okay" + compatible="myvendor,myled-v1"
  -> platform device exists
  -> platform bus match
  -> probe callback

User sysfs event:
  user read/write file
  -> sysfs attribute callback
  -> dev_get_drvdata
  -> register helper
  -> simulated array or MMIO

PM event:
  system suspend
  -> dev_pm_ops suspend callback
  -> clear ENABLE
  system resume
  -> dev_pm_ops resume callback
  -> set ENABLE
```

#### Ownership Transfer

目前程式碼中未觀察到跨 subsystem 的 explicit buffer ownership transfer。可驗證的 ownership 主要是：

- `priv` 與 `base` 交給 devm framework 管理。
- sysfs group 由 driver create/remove 成對管理。
- `pdev/dev` 保存 `priv` pointer，但不取得 `priv` 的手動釋放責任。

---

## 第二階段：Architecture / API Technical Report

### 1. Entry Point 行為

#### # Direct Observation

此 driver 的 kernel entry point 不是傳統顯式 `module_init()`，而是 `module_platform_driver(myled_driver)`。它把 `myled_driver` 交給 platform driver framework，並由 framework 在 module load/unload 時註冊/反註冊 driver。

在此專案的 QEMU demo flow 中，module load 由 `rootfs/overlay/init:16` 的 `insmod /myled_ctrl.ko` 觸發；不是從目前 rootfs script 觀察到的 udev/modprobe 自動載入。`MODULE_DEVICE_TABLE(of, myled_of_match)` 存在，但「是否會被 userspace 自動載入」無法從現有 rootfs 內容確認，因為目前 `/init` 使用的是直接 `insmod`。

`myled_probe` 是 driver 被 platform bus bind 後的實際初始化入口。它的行為依序是：

1. 分配 `struct myled_priv`。
2. 初始化 back pointer 與 spinlock。
3. 讀取 Device Tree properties。
4. 取得與 mapping MEM resource。
5. 設定 simulated fallback state。
6. 綁定 drvdata。
7. 初始化 register state。
8. 建立 sysfs group。
9. 啟用 runtime PM framework。

---

### 2. Callback Registration Chain

#### Platform / OF Callback Registration

```text
myled_driver
  .probe = myled_probe
  .remove = myled_remove
  .driver.name = "myled_ctrl"
  .driver.of_match_table = myled_of_match
  .driver.pm = &myled_pm_ops
```

`myled_of_match` 裡的 `"myvendor,myled-v1"` 對應到 `dts/myled-fragment.dts:27` 的 compatible。這是 probe 能發生的直接證據。第二個 compatible `"myvendor,myled"` 在目前 DTS 中未使用，但程式碼支援。

#### Sysfs Callback Registration

`DEVICE_ATTR_RW/RO` macro 會產生 `dev_attr_*` 物件，`myled_attrs[]` 把它們聚合，`myled_attr_group` 則把 group name 設為 `"myled"`。`myled_probe` 呼叫 `sysfs_create_group(&dev->kobj, &myled_attr_group)` 後，使用者才有 `/sys/bus/platform/devices/<DEV>/myled/*` 可操作。

callback chain 如下：

```text
sysfs file read/write
  -> struct device_attribute generated by DEVICE_ATTR_*
  -> *_show or *_store
  -> dev_get_drvdata(dev)
  -> register helper
```

#### PM Callback Registration

`myled_pm_ops` 使用 `SET_SYSTEM_SLEEP_PM_OPS(myled_suspend, myled_resume)`。可直接驗證的是 system sleep callback；目前程式碼中未觀察到 runtime PM operation table callback。

---

### 3. Runtime Dispatch Flow

#### sysfs `enable`

- `enable_show`：讀 `MYLED_REG_CTRL`，輸出 enable bit。
- `enable_store`：`kstrtobool` parse，true 時 set `MYLED_CTRL_ENABLE`，false 時 clear。
- error handling：parse 失敗直接回傳錯誤；不更改 register。

#### sysfs `brightness`

- `brightness_show`：讀 `MYLED_REG_BRIGHTNESS`。
- `brightness_store`：`kstrtou32` 以 base 10 parse；若大於 `MYLED_MAX_BRIGHTNESS` 則回傳 `-EINVAL`；成功才寫 register。
- lifecycle role：probe 時 `myled_hw_init` 也會寫入 initial brightness；remove/shutdown 時歸零。

#### sysfs `color`

- `color_show`：讀 `MYLED_REG_COLOR`，mask 成 24-bit，輸出 `%06x`。
- `color_store`：`kstrtou32(buf, 16, &val)` parse hex，寫入 `val & 0xFFFFFF`。
- error handling：parse 失敗直接回傳錯誤。

#### sysfs `blink`

- `blink_show`：讀 CTRL blink bit。
- `blink_store`：`kstrtobool` parse 後 set/clear `MYLED_CTRL_BLINK`。
- cleanup role：`myled_hw_shutdown` 會清掉 blink bit。

#### sysfs `status`

- `status_show`：讀 `MYLED_REG_STATUS`，輸出 `ready` 與 `fault`。
- 此 attribute 是 read-only，沒有 store path。
- 在 simulated fallback 時，`myled_hw_init` 會設定 `MYLED_STATUS_READY`。

#### sysfs `info`

- `info_show`：讀 VERSION、CTRL、BRIGHTNESS、COLOR，再輸出 `num_leds`、`simulated` 與 bit decode。
- 此 attribute 是 read-only，扮演 snapshot/debug interface。

---

### 4. Indirect Call Path

#### # Direct Observation

此專案的 indirect dispatch 主要有四類：

1. platform driver core 透過 `myled_driver.probe/remove` 呼叫本 driver。
2. OF matching 透過 `myled_driver.driver.of_match_table` 比對 DTS compatible。
3. sysfs core 透過 `dev_attr_*` 的 generated show/store callback 呼叫本 driver。
4. PM core 透過 `myled_pm_ops` 呼叫 suspend/resume。

目前程式碼中未觀察到 IRQ dispatch table、file_operations、miscdevice operations、net_device_ops、workqueue callback 或 timer callback。

---

### 5. Resource Lifecycle

#### Managed Resource Model

`devm_kzalloc` 與 `devm_ioremap_resource` 表示 `priv` 與 MMIO mapping 都不在 `myled_remove` 中手動釋放。`myled_remove` 只處理 framework 不會自動替 driver 做的 logical cleanup：disable PM runtime、remove sysfs group、shutdown hardware state。

#### sysfs Lifecycle

```text
myled_probe
  -> sysfs_create_group
  -> userspace can access myled attributes

myled_remove
  -> sysfs_remove_group
  -> userspace path disappears
```

`sysfs_create_group` 若失敗，程式碼會呼叫 `myled_hw_shutdown(priv)` 後回傳錯誤。由於 sysfs 尚未建立，該錯誤路徑不需要 `sysfs_remove_group`。

#### Register Backend Lifecycle

初始化時有兩種 backend：

- MMIO backend：`platform_get_resource` 成功且 `devm_ioremap_resource` 成功，先嘗試讀 VERSION。
- simulated backend：缺 MEM resource、ioremap 失敗、或 VERSION 不是 `MYLED_HW_VERSION` 時啟用。

`myled_hw_init` 裡若 VERSION mismatch，會設定：

```text
priv->simulated = true
sim_regs[VERSION / 4] = MYLED_HW_VERSION
sim_regs[STATUS / 4] = MYLED_STATUS_READY
```

然後 brightness 與 enable 都透過 register helper 寫入，因此會寫到目前選定的 backend。

---

### 6. Error Propagation Path

#### Probe Error Handling

| 錯誤點 | 目前行為 | 是否中止 probe |
|---|---|---|
| `devm_kzalloc` 失敗 | return `-ENOMEM` | 是 |
| `of_property_read_u32(..., "num-leds")` 失敗 | warn，`priv->num_leds = 1` | 否 |
| `of_property_read_string(..., "label")` 失敗 | 不印 label | 否 |
| `platform_get_resource` 失敗 | warn，`priv->simulated = true` | 否 |
| `devm_ioremap_resource` 失敗 | warn，`base = NULL`，`simulated = true` | 否 |
| VERSION mismatch | warn，切 simulated mode，seed VERSION/STATUS | 否 |
| `myled_hw_init` return non-zero | dev_err，return error | 是；但目前 `myled_hw_init` 實作固定 return 0 |
| `sysfs_create_group` 失敗 | dev_err，`myled_hw_shutdown`，return error | 是 |

#### sysfs Error Handling

| Attribute | parse / validation | error |
|---|---|---|
| `enable` | `kstrtobool` | parse 失敗回傳 ret |
| `brightness` | `kstrtou32` base 10；`val <= 255` | parse 失敗回傳 ret；超過 255 回傳 `-EINVAL` |
| `color` | `kstrtou32` base 16 | parse 失敗回傳 ret；成功後 mask 24-bit |
| `blink` | `kstrtobool` | parse 失敗回傳 ret |

#### Cleanup Error Handling

`myled_remove` 沒有檢查 `priv` 是否為 `NULL`。在正常 platform driver flow 中，`priv` 應由 successful probe 設定；若 probe 未完成而呼叫 remove，通常 driver core 不會對未 bind 成功的 device 呼叫 remove。這是基於 Linux driver model 的保守推論，不是此專案內部額外防護。

---

### 7. Synchronization Role

#### # Direct Observation

`myled_reg_read` 與 `myled_reg_write` 都使用：

```text
spin_lock_irqsave(&priv->lock, flags)
...
spin_unlock_irqrestore(&priv->lock, flags)
```

保護的共享狀態是：

- `priv->sim_regs[]`。
- MMIO access sequence 中的 `readl/writel` 呼叫。

`irqsave` 可避免同 CPU interrupt context 在持鎖期間重入同一把 lock。不過目前程式碼中未觀察到任何 IRQ handler，因此這個選擇比較像保守保護，而不是已存在 IRQ path 的必要需求。

#### # Conservative Inference

sysfs show/store 可能由不同 process 同時呼叫；因此使用 spinlock 能避免單次 read 或 write 對 `sim_regs` 的資料競爭。不過 `myled_reg_set_bits` 與 `myled_reg_clr_bits` 是由「一次 read helper + 一次 write helper」組成，整個 read-modify-write 沒有被同一個 lock critical section 包住。若兩個 sysfs writers 同時修改不同 CTRL bit，存在 lost update 的可能。這不是從測試腳本必然觸發的 bug，但從 helper 實作可直接看出風險。

---

### 8. 比較分析

#### 類似 API 行為：`enable_store` vs `blink_store`

兩者都：

- 使用 `dev_get_drvdata(dev)` 取回 `priv`。
- 使用 `kstrtobool` parse input。
- 依 bool 結果呼叫 `myled_reg_set_bits` 或 `myled_reg_clr_bits`。
- 作用於 `MYLED_REG_CTRL`。

差異：

- `enable_store` 操作 `MYLED_CTRL_ENABLE`，並呼叫 `dev_dbg` 記錄 enabled/disabled。
- `blink_store` 操作 `MYLED_CTRL_BLINK`，沒有 debug log。

使用原因只能從 code 行為說明：兩者都是 CTRL bit toggling path，因此共用 bit helper；enable 另外記錄 log，blink 沒有。無法從現有內容確認為何 blink 不記錄 log。

#### 類似 API 行為：`brightness_store` vs `color_store`

兩者都：

- 使用 `kstrtou32` parse 數值。
- 寫入單一 register。

差異：

- `brightness_store` 使用 base 10，且檢查 `val <= MYLED_MAX_BRIGHTNESS`。
- `color_store` 使用 base 16，並以 `val & 0xFFFFFF` 限制 RRGGBB。

使用原因可從 code 直接看出：brightness register 的合法範圍由 `MYLED_MAX_BRIGHTNESS` 限制；color 則保留低 24 bits 對應 `%06x` 顯示格式。

#### Callback 機制差異：Platform callback vs Sysfs callback vs PM callback

| 機制 | 註冊位置 | 觸發來源 | data 取得方式 | lifecycle role |
|---|---|---|---|---|
| platform callback | `myled_driver.probe/remove` | driver core bind/unbind | `struct platform_device *pdev` | 建立/清理 device instance。 |
| sysfs callback | `DEVICE_ATTR_*` + `sysfs_create_group` | userspace read/write sysfs | `dev_get_drvdata(dev)` | runtime control/data observation。 |
| PM callback | `myled_pm_ops` | system sleep/resume | `dev_get_drvdata(dev)` | suspend/resume 時調整 enable state。 |

#### Dispatch Model

此 driver 使用 Linux framework dispatch，而不是手寫 event loop：

- platform bus dispatch：以 OF compatible 和 `platform_driver` operation table 為核心。
- sysfs dispatch：以 attribute table 為核心。
- PM dispatch：以 `dev_pm_ops` 為核心。

目前程式碼中未觀察到 user-defined dispatch loop 或 command parser。

#### Resource Management Model

| Resource | 管理方式 | cleanup 位置 |
|---|---|---|
| `struct myled_priv` | `devm_kzalloc` | driver core/devm 自動釋放 |
| MMIO mapping | `devm_ioremap_resource` | driver core/devm 自動釋放 |
| sysfs group | explicit create/remove | `myled_probe` / `myled_remove` |
| hardware logical state | explicit init/shutdown | `myled_hw_init` / `myled_hw_shutdown` |
| initramfs build artifact | shell script copy/pack | `scripts/06_clean.sh` 可清 |

使用原因只能依 code 說明：記憶體與 MMIO 使用 devm API，所以 remove 不需要手動 free/unmap；sysfs 與 hardware state 則有 explicit API/state change，因此 driver 自己在 remove path 做對應 cleanup。

---

### 9. Debug / Risk Analysis

#### Potential Memory Leak

- `priv` 使用 `devm_kzalloc`，正常 remove path 未觀察到手動 `kfree` 缺失造成的 leak。
- `base` 使用 `devm_ioremap_resource`，正常 remove path 未觀察到手動 `iounmap` 缺失造成的 leak。
- `sysfs_create_group` 成功後，`myled_remove` 有 `sysfs_remove_group`。若 `sysfs_create_group` 失敗，程式碼會 `myled_hw_shutdown` 並 return error；沒有建立 sysfs group，因此不需要 remove group。
- 目前程式碼中未觀察到額外 `kmalloc`、`alloc_chrdev_region`、`device_create`、`class_create` 等需要額外 unwind 的 resource。

#### Invalid Ownership Transfer

- 目前程式碼中未觀察到把 stack memory 或 freed memory 傳給 callback 保存的情況。
- `label` 是在 `myled_probe` 的 block 內由 `of_property_read_string` 取出後只用於 `dev_info`，沒有保存到 `priv`；因此沒有 string ownership 問題。
- `priv` 同時透過 `platform_set_drvdata` 與 `dev_set_drvdata` 綁定。兩者指向同一個 devm-managed object；正常 flow 下沒有雙重釋放，因為 driver 沒有手動 free。

#### Callback Misuse Risk

- sysfs callbacks 假設 `dev_get_drvdata(dev)` 一定回傳有效 `priv`。因為 `myled_probe` 在 `sysfs_create_group` 前已呼叫 `dev_set_drvdata`，正常 create 後的 sysfs callback 可取得 `priv`。
- `myled_remove` 順序是先 `sysfs_remove_group` 再 `myled_hw_shutdown`，可降低 remove 後新的 sysfs callback 進入 driver 的機會。
- 目前程式碼中未觀察到 explicit reference counting 或 open/close model；sysfs core 本身處理 attribute callback 的 kobject lifecycle。此處不額外推測 kernel core 細節。

#### Lifecycle Mismatch

- `pm_runtime_enable(dev)` 有在 `myled_remove` 用 `pm_runtime_disable(dev)` 對應。
- 目前程式碼有啟用 runtime PM，但沒有 runtime PM callbacks。這不一定是錯誤，但 report 必須標示：目前只能驗證 system sleep suspend/resume 行為，無法從現有程式碼確認 runtime suspend/resume 實際狀態轉換。
- `myled_hw_init` 的 VERSION mismatch 會切到 simulated mode，但若 `devm_ioremap_resource` 已成功，`base` mapping 仍存在直到 devm cleanup；這是 devm-managed lifetime，不是 leak。不過 runtime register path 會因 `priv->simulated = true` 改用 `sim_regs`，不再使用 mapped `base`。

#### Concurrency Issue

- 有 evidence 的同步機制：`myled_reg_read/write` 使用 `spin_lock_irqsave`。
- 有 evidence 的風險：`myled_reg_set_bits` / `myled_reg_clr_bits` 的 read-modify-write 不是單一 critical section。兩個 concurrent sysfs writers 若同時修改 CTRL register 不同 bit，可能出現 lost update。例如一個 writer set ENABLE，另一個 writer set BLINK，兩者都先讀到舊值，再各自寫回不同 bit 組合，最後可能只保留其中一個 bit。
- 目前程式碼中未觀察到 IRQ handler，因此無法主張已有 IRQ 與 sysfs 競爭；只能說 lock 使用了 irqsave，但 IRQ path 不存在於目前 codebase。

#### Build / Script Risk

- `scripts/04_build_rootfs.sh:18` 的 `BUSYBOX` 是固定到使用者路徑 `~/桌面/Linux-kernel/qemu-platform-demo/busybox-1.36.1/busybox`。這是 script 可攜性風險；同檔案中原本有較通用的 busybox discovery 寫法但被註解。此點來自 script 直接觀察。
- `rootfs/overlay/test_myled.sh` 以 `grep "0d000000"` 尋找 device。若 `/sys/bus/platform/devices` 下有多個名稱包含 `0d000000` 的 device，script 會取第一個。現有 DTS 只建立一個 `myled-controller@0d000000`，但 script 本身不是嚴格 match exact device name。
- `rootfs/overlay/init` 與部分 scripts 目前存在文字編碼亂碼；不影響此報告對 shell command 的結構分析，但可能影響 demo 顯示訊息可讀性。

---

## 補充：Register / DT / Sysfs 對照

### Register Macro

| Macro | 定義位置 | 值 | 使用角色 |
|---|---|---:|---|
| `MYLED_REG_CTRL` | `driver/myled_ctrl.h:10` | `0x00` | enable/blink/pwm bit register。 |
| `MYLED_REG_BRIGHTNESS` | `driver/myled_ctrl.h:11` | `0x04` | brightness register。 |
| `MYLED_REG_COLOR` | `driver/myled_ctrl.h:12` | `0x08` | 24-bit color register。 |
| `MYLED_REG_STATUS` | `driver/myled_ctrl.h:13` | `0x0C` | ready/fault status register。 |
| `MYLED_REG_VERSION` | `driver/myled_ctrl.h:14` | `0x10` | hardware version register。 |
| `MYLED_CTRL_ENABLE` | `driver/myled_ctrl.h:17` | `BIT(0)` | enable control bit。 |
| `MYLED_CTRL_BLINK` | `driver/myled_ctrl.h:18` | `BIT(1)` | blink control bit。 |
| `MYLED_CTRL_PWM_AUTO` | `driver/myled_ctrl.h:19` | `BIT(2)` | shutdown 時會清除；目前沒有 sysfs store path 設定它。 |
| `MYLED_STATUS_READY` | `driver/myled_ctrl.h:22` | `BIT(0)` | simulated fallback 時設定。 |
| `MYLED_STATUS_FAULT` | `driver/myled_ctrl.h:23` | `BIT(1)` | `status_show` 會讀取；目前程式碼中未觀察到任何 path 設定此 bit。 |
| `MYLED_MAX_BRIGHTNESS` | `driver/myled_ctrl.h:26` | `255U` | brightness validation / clamp。 |
| `MYLED_HW_VERSION` | `driver/myled_ctrl.h:27` | `0xAB01U` | VERSION check 與 simulated seed。 |
| `MYLED_SIM_REG_COUNT` | `driver/myled_ctrl.h:28` | `8` | `sim_regs` array 大小。 |

### Device Tree Property

| Property | DTS 位置 | Driver 使用位置 | 影響 |
|---|---|---|---|
| `compatible = "myvendor,myled-v1"` | `dts/myled-fragment.dts:27` | `myled_of_match` | 觸發 OF/platform match。 |
| `reg = <0x0 0x0d000000 0x0 0x1000>` | `dts/myled-fragment.dts:28` | `platform_get_resource` / `devm_ioremap_resource` | 提供 MMIO base/size。 |
| `num-leds = <4>` | `dts/myled-fragment.dts:29` | `of_property_read_u32` in `myled_probe` | 設定 `priv->num_leds`。 |
| `label = "demo-rgb-led"` | `dts/myled-fragment.dts:30` | `of_property_read_string` in `myled_probe` | 只用於 log。 |
| `default-brightness = <180>` | `dts/myled-fragment.dts:31` | `of_property_read_u32` in `myled_hw_init` | 初始化 brightness，並 clamp 到 255。 |
| `status = "okay"` | `dts/myled-fragment.dts:32` | kernel DT/platform population | 啟用 node；driver 沒有直接讀此 property。 |

### Sysfs Attribute

| Attribute | show | store | Register / State | Test script evidence |
|---|---|---|---|---|
| `enable` | `enable_show` | `enable_store` | `MYLED_REG_CTRL` / `MYLED_CTRL_ENABLE` | `test_myled.sh:56-58` 寫 0 後讀回。 |
| `brightness` | `brightness_show` | `brightness_store` | `MYLED_REG_BRIGHTNESS` | `test_myled.sh:38-40` 寫 200 後讀回。 |
| `color` | `color_show` | `color_store` | `MYLED_REG_COLOR` low 24-bit | `test_myled.sh:44-46` 寫 `ff3300` 後讀回。 |
| `blink` | `blink_show` | `blink_store` | `MYLED_REG_CTRL` / `MYLED_CTRL_BLINK` | `test_myled.sh:50-52` 寫 1 後讀回。 |
| `status` | `status_show` | 目前沒有 | `MYLED_REG_STATUS` | `test_myled.sh:32-33` 讀取。 |
| `info` | `info_show` | 目前沒有 | VERSION/CTRL/BRIGHTNESS/COLOR + `num_leds`/`simulated` | `test_myled.sh:62` dump。 |

---

## 結論

`qemu-platform-demo` 目前實作的是一個以 Device Tree match 觸發的 Linux platform driver demo。核心 execution semantics 是：QEMU 使用 patched DTB 產生 `myled-controller@0d000000` platform device；rootfs `/init` 手動 `insmod` driver；platform bus 根據 compatible 呼叫 `myled_probe`；probe 建立 devm-managed private state、解析 DT、嘗試 MMIO mapping，若無法確認 hardware version 則切換到 simulated shadow register bank；接著建立 named sysfs group，讓 userspace 透過 `enable`、`brightness`、`color`、`blink`、`status`、`info` 間接操作或觀察 register state。

此專案最重要的 callback chain 是 platform driver callback、sysfs attribute callback 與 system sleep PM callback。ownership 上沒有複雜 buffer transfer，主要依賴 devm 管理 `priv` 與 MMIO mapping，並由 explicit remove path 管理 sysfs group 與 logical hardware shutdown。可驗證風險集中在 read-modify-write helper 的 lock granularity、runtime PM callback 缺席但 runtime PM 被 enable、以及 rootfs build script 中 hard-coded BusyBox path。
