#!/bin/bash
# 07_fix_busybox_arch.sh
#
# 問題：initramfs 裡的 busybox 是 x86-64，
#       ARM64 kernel 無法執行 /bin/sh，導致 init 直接失敗。
# 解法：從 source 交叉編譯出 ARM64 static busybox，
#       放到 tools/busybox-aarch64，供 04_build_rootfs.sh 使用。

set -euo pipefail

# ── 顏色 ────────────────────────────────────────────────────────────
RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'
CYAN='\033[0;36m'; NC='\033[0m'
info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
die()     { echo -e "${RED}[ERR]${NC}   $*"; exit 1; }

# ── 路徑：scripts/ 的上一層就是專案根目錄 ──────────────────────────
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

BUSYBOX_VER="1.36.1"
BUSYBOX_DIR="busybox-${BUSYBOX_VER}"
BUSYBOX_TAR="busybox-${BUSYBOX_VER}.tar.bz2"
BUSYBOX_URL="https://busybox.net/downloads/${BUSYBOX_TAR}"
TOOLS_DIR="${PROJECT_ROOT}/tools"
OUTPUT="${TOOLS_DIR}/busybox-aarch64"
CROSS="aarch64-linux-gnu-"

# ════════════════════════════════════════════════════════════════════
info "=== Step 1: 確認 cross-compiler ==="
# ════════════════════════════════════════════════════════════════════
if ! command -v ${CROSS}gcc &>/dev/null; then
    warn "${CROSS}gcc 不存在，嘗試安裝 ..."
    sudo apt-get install -y gcc-aarch64-linux-gnu \
        || die "安裝 cross-compiler 失敗，請手動執行：sudo apt-get install gcc-aarch64-linux-gnu"
fi
info "Cross-compiler: $(${CROSS}gcc --version | head -1)"

# ════════════════════════════════════════════════════════════════════
info "=== Step 2: 下載 BusyBox ${BUSYBOX_VER} source ==="
# ════════════════════════════════════════════════════════════════════
if [ ! -f "${BUSYBOX_TAR}" ]; then
    info "下載 ${BUSYBOX_URL} ..."
    wget -q --show-progress "${BUSYBOX_URL}" \
        || die "下載失敗，請確認網路或手動下載到 ${PROJECT_ROOT}/"
else
    info "已存在 ${BUSYBOX_TAR}，跳過下載"
fi

if [ ! -d "${BUSYBOX_DIR}" ]; then
    info "解壓縮 ..."
    tar xf "${BUSYBOX_TAR}"
fi

cd "${BUSYBOX_DIR}"

# ════════════════════════════════════════════════════════════════════
info "=== Step 3: defconfig (ARM64) ==="
# ════════════════════════════════════════════════════════════════════
make ARCH=arm64 CROSS_COMPILE="${CROSS}" defconfig \
    || die "defconfig 失敗"

# ════════════════════════════════════════════════════════════════════
info "=== Step 4: 強制 static linking ==="
# ════════════════════════════════════════════════════════════════════
# defconfig 預設是動態連結，initramfs 裡沒有 libc.so，必須 static
if grep -q "# CONFIG_STATIC is not set" .config; then
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
elif ! grep -q "CONFIG_STATIC=y" .config; then
    echo "CONFIG_STATIC=y" >> .config
fi

grep "CONFIG_STATIC" .config | grep -v "LIBGCC" \
    && success "CONFIG_STATIC=y 確認" \
    || die "CONFIG_STATIC 設定失敗"

# ════════════════════════════════════════════════════════════════════
info "=== Step 5: 關掉會編譯失敗的 applet ==="
# ════════════════════════════════════════════════════════════════════
# tc applet 用到 kernel header 裡不完整的結構體，ARM64 cross-build 會炸
BROKEN_APPLETS=("CONFIG_TC")

for applet in "${BROKEN_APPLETS[@]}"; do
    if grep -q "${applet}=y" .config; then
        sed -i "s/${applet}=y/# ${applet} is not set/" .config
        info "已關閉 ${applet}"
    else
        info "${applet} 本來就是關閉的，跳過"
    fi
done

# ════════════════════════════════════════════════════════════════════
info "=== Step 6: 編譯 ($(nproc) jobs) ==="
# ════════════════════════════════════════════════════════════════════
make ARCH=arm64 CROSS_COMPILE="${CROSS}" -j"$(nproc)" 2>&1 \
    | tee /tmp/busybox_build.log \
    | grep -E "^(  (LINK|STRIP|CC)|make\[|networking/tc|error:)" || true

# 確認 binary 真的產生了
if [ ! -f "busybox" ]; then
    warn "busybox binary 不存在，查看完整 log："
    tail -30 /tmp/busybox_build.log
    die "編譯失敗，詳細 log 在 /tmp/busybox_build.log"
fi

# ════════════════════════════════════════════════════════════════════
info "=== Step 7: 驗證架構 ==="
# ════════════════════════════════════════════════════════════════════
FILEINFO=$(file busybox)
info "file output: ${FILEINFO}"

if echo "${FILEINFO}" | grep -q "ARM aarch64"; then
    success "架構正確：ARM aarch64"
else
    die "架構錯誤！得到：${FILEINFO}\n請確認 CROSS_COMPILE=${CROSS} 是否正確安裝"
fi

if echo "${FILEINFO}" | grep -q "statically linked"; then
    success "Statically linked 確認"
else
    die "不是 static binary！initramfs 執行會失敗"
fi

# ════════════════════════════════════════════════════════════════════
info "=== Step 8: 複製到 tools/ ==="
# ════════════════════════════════════════════════════════════════════
mkdir -p "${TOOLS_DIR}"
cp busybox "${OUTPUT}"
chmod +x "${OUTPUT}"

success "BusyBox ARM64 binary → ${OUTPUT}"

# ════════════════════════════════════════════════════════════════════
info "=== Step 9: 更新 04_build_rootfs.sh 的 BUSYBOX 路徑 ==="
# ════════════════════════════════════════════════════════════════════
ROOTFS_SCRIPT="${PROJECT_ROOT}/scripts/04_build_rootfs.sh"

# 把原本的 BUSYBOX= 那幾行換成固定指向 tools/busybox-aarch64
OLD='BUSYBOX=\$(which busybox-aarch64.*\n.*\n.*busybox\)'
NEW="BUSYBOX=\"\${PROJECT_ROOT}/tools/busybox-aarch64\""

# 用 Python 做多行取代（bash sed 處理多行很麻煩）
python3 - <<'PYEOF'
import re, sys

path = "scripts/04_build_rootfs.sh"
text = open(path).read()

# 找到 BUSYBOX= 那整個區塊換掉
pattern = r'BUSYBOX=\$\(which busybox-aarch64.*?echo busybox\)\)'
replacement = 'BUSYBOX="${PROJECT_ROOT}/tools/busybox-aarch64"'

new_text, count = re.subn(pattern, replacement, text, flags=re.DOTALL)
if count == 0:
    # 可能已經改過了，或格式不同，直接確認就好
    print("[WARN] 04_build_rootfs.sh 的 BUSYBOX 行未匹配，請手動確認")
else:
    open(path, 'w').write(new_text)
    print("[OK]   04_build_rootfs.sh BUSYBOX 路徑已更新")
PYEOF

# ── 同時確保 04_build_rootfs.sh 有 PROJECT_ROOT 變數 ──────────────
if ! grep -q "PROJECT_ROOT" "${ROOTFS_SCRIPT}"; then
    sed -i '2i PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"' \
        "${ROOTFS_SCRIPT}"
    info "已在 04_build_rootfs.sh 加入 PROJECT_ROOT 定義"
fi

# ════════════════════════════════════════════════════════════════════
echo ""
echo -e "${GREEN}════════════════════════════════════════${NC}"
echo -e "${GREEN}  BusyBox ARM64 build 完成！            ${NC}"
echo -e "${GREEN}════════════════════════════════════════${NC}"
echo ""
echo "  Binary : ${OUTPUT}"
echo "  Size   : $(du -sh "${OUTPUT}" | cut -f1)"
echo ""
echo "下一步："
echo "  bash scripts/04_build_rootfs.sh"
echo "  bash scripts/05_run_qemu.sh"
echo ""
