#!/bin/sh
# myled_ctrl sysfs 自測。
# 只使用 BusyBox ash 語法，確保 initramfs 內可直接執行。

SYSFS_BASE="/sys/bus/platform/devices"
EXPECTED_COMPAT="myvendor,myled-v1"
EXPECTED_NODE="myled-controller@0d000000"

FAILS=0

pass() {
    echo "  [PASS] $1"
}

fail() {
    echo "  [FAIL] $1"
    FAILS=$((FAILS + 1))
}

find_myled_device() {
    # Kernel 產生 platform device name 時可能省略 leading zero。
    # 先檢查兩個固定候選，再用 compatible 做保底搜尋。
    for d in \
        "${SYSFS_BASE}/0d000000.myled-controller" \
        "${SYSFS_BASE}/d000000.myled-controller"
    do
        [ -d "$d" ] || continue
        [ -e "$d/modalias" ] || continue
        grep -q "$EXPECTED_COMPAT" "$d/modalias" 2>/dev/null && {
            echo "$d"
            return 0
        }
    done

    for d in "${SYSFS_BASE}"/*; do
        [ -d "$d" ] || continue
        [ -e "$d/modalias" ] || continue
        grep -q "$EXPECTED_COMPAT" "$d/modalias" 2>/dev/null && {
            echo "$d"
            return 0
        }
    done

    return 1
}

read_attr() {
    attr=$1

    if [ ! -f "$MYLED/$attr" ]; then
        fail "$attr missing"
        return 1
    fi

    cat "$MYLED/$attr"
}

check_attr_exists() {
    attr=$1

    if [ -f "$MYLED/$attr" ]; then
        pass "$attr exists"
    else
        fail "$attr missing"
    fi
}

write_and_check() {
    attr=$1
    value=$2
    expected=$3
    actual=""

    if ! echo "$value" > "$MYLED/$attr" 2>/dev/null; then
        fail "write $attr=$value"
        return
    fi

    actual=$(cat "$MYLED/$attr" 2>/dev/null)
    if [ "$actual" = "$expected" ]; then
        pass "$attr write/readback = $actual"
    else
        fail "$attr readback expected $expected got $actual"
    fi
}

DEV_PATH=$(find_myled_device)
if [ -z "$DEV_PATH" ]; then
    echo "[FAIL] device not found for compatible $EXPECTED_COMPAT"
    exit 1
fi

DEV=${DEV_PATH##*/}
MYLED="${DEV_PATH}/myled"

echo "=================================================="
echo " Expected DT : $EXPECTED_NODE"
echo " Device      : $DEV"
echo " Path        : $DEV_PATH"
echo " Sysfs node  : $MYLED"
echo "=================================================="
echo ""

case "$DEV" in
    0d000000.myled-controller|d000000.myled-controller)
        pass "platform device name = $DEV"
        ;;
    *)
        fail "unexpected platform device name = $DEV"
        ;;
esac

# sysfs 裝置名稱可能少掉 leading zero；of_node 才是 DT 節點是否正確的依據。
if [ -L "$DEV_PATH/of_node" ]; then
    OF_TARGET=$(readlink "$DEV_PATH/of_node" 2>/dev/null)
    case "$OF_TARGET" in
        *"$EXPECTED_NODE")
            pass "of_node = $EXPECTED_NODE"
            ;;
        *)
            fail "of_node target expected $EXPECTED_NODE got $OF_TARGET"
            ;;
    esac
else
    fail "of_node link missing"
fi

if [ -L "$DEV_PATH/driver" ]; then
    pass "driver bound"
else
    fail "driver not bound"
fi

if [ -d "$MYLED" ]; then
    pass "sysfs directory ready"
else
    fail "sysfs directory missing"
    echo "Hint: dmesg | grep myled"
    exit 1
fi

echo ""
echo "read test"
for a in info enable brightness color blink status; do
    check_attr_exists "$a"
done

echo ""
echo "status"
read_attr status | sed 's/^/  /'

echo ""
echo "write test"
write_and_check brightness 200 200
write_and_check color ff3300 ff3300
write_and_check blink 1 1
write_and_check enable 0 0

echo ""
echo "info"
read_attr info | sed 's/^/  /'

echo ""
echo "dmesg"
dmesg | grep -i myled 2>/dev/null | sed 's/^/  /'

echo ""
if [ "$FAILS" -eq 0 ]; then
    echo "[PASS] myled sysfs test completed"
    exit 0
fi

echo "[FAIL] myled sysfs test failed ($FAILS failures)"
exit 1
