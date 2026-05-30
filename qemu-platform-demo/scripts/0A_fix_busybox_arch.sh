#!/bin/bash
# 建置 ARM64 static BusyBox，供 initramfs 使用。
#
# 背景：ARM64 kernel 不能執行 x86-64 BusyBox。
# 產物：tools/busybox-aarch64，供 scripts/04_build_rootfs.sh 自動取用。

set -euo pipefail

# 顏色輸出：只讓步驟訊息比較容易讀。
RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'
CYAN='\033[0;36m'; NC='\033[0m'
info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
die()     { echo -e "${RED}[ERR]${NC}   $*"; exit 1; }

# scripts/ 的上一層就是專案根目錄，後續路徑都從這裡展開。
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

BUSYBOX_VER="1.36.1"
BUSYBOX_DIR="busybox-${BUSYBOX_VER}"
BUSYBOX_TAR="busybox-${BUSYBOX_VER}.tar.bz2"
BUSYBOX_URL="https://busybox.net/downloads/${BUSYBOX_TAR}"
TOOLS_DIR="${PROJECT_ROOT}/tools"
OUTPUT="${TOOLS_DIR}/busybox-aarch64"
CROSS="aarch64-linux-gnu-"

info "=== Step 1: 確認 cross-compiler ==="
if ! command -v ${CROSS}gcc &>/dev/null; then
    warn "${CROSS}gcc 不存在，嘗試安裝 ..."
    sudo apt-get install -y gcc-aarch64-linux-gnu \
        || die "安裝 cross-compiler 失敗，請手動執行：sudo apt-get install gcc-aarch64-linux-gnu"
fi
info "Cross-compiler: $(${CROSS}gcc --version | head -1)"

info "=== Step 2: 下載 BusyBox ${BUSYBOX_VER} source ==="
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

info "=== Step 3: defconfig (ARM64) ==="
make ARCH=arm64 CROSS_COMPILE="${CROSS}" defconfig \
    || die "defconfig 失敗"

info "=== Step 4: 強制 static linking ==="
# initramfs 沒有動態 linker/libc，所以 BusyBox 必須 static link。
if grep -q "# CONFIG_STATIC is not set" .config; then
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
elif ! grep -q "CONFIG_STATIC=y" .config; then
    echo "CONFIG_STATIC=y" >> .config
fi

grep "CONFIG_STATIC" .config | grep -v "LIBGCC" \
    && success "CONFIG_STATIC=y 確認" \
    || die "CONFIG_STATIC 設定失敗"

info "=== Step 5: 關掉會編譯失敗的 applet ==="
# tc applet 依賴較複雜的 network headers；本 demo 不需要，直接關閉。
BROKEN_APPLETS=("CONFIG_TC")

for applet in "${BROKEN_APPLETS[@]}"; do
    if grep -q "${applet}=y" .config; then
        sed -i "s/${applet}=y/# ${applet} is not set/" .config
        info "已關閉 ${applet}"
    else
        info "${applet} 本來就是關閉的，跳過"
    fi
done

info "=== Step 6: 編譯 ($(nproc) jobs) ==="
make ARCH=arm64 CROSS_COMPILE="${CROSS}" -j"$(nproc)" 2>&1 \
    | tee /tmp/busybox_build.log \
    | grep -E "^(  (LINK|STRIP|CC)|make\[|networking/tc|error:)" || true

# 編譯 log 已被篩選顯示；失敗時再提示完整 log 位置。
if [ ! -f "busybox" ]; then
    warn "busybox binary 不存在，查看完整 log："
    tail -30 /tmp/busybox_build.log
    die "編譯失敗，詳細 log 在 /tmp/busybox_build.log"
fi

info "=== Step 7: 驗證架構 ==="
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

info "=== Step 8: 複製到 tools/ ==="
mkdir -p "${TOOLS_DIR}"
cp busybox "${OUTPUT}"
chmod +x "${OUTPUT}"

success "BusyBox ARM64 binary: ${OUTPUT}"

info "=== Step 9: Rootfs builder input ==="
echo "scripts/04_build_rootfs.sh 會自動使用 ${OUTPUT}。"
echo "若要改用其他 BusyBox，可設定 BUSYBOX=/path/to/aarch64/busybox。"

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
