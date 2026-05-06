#!/bin/sh
# test_myled.sh — sysfs demo script

SYSFS_BASE="/sys/bus/platform/devices"
#DEV=$(ls ${SYSFS_BASE} | grep "10010000" | head -1)
DEV=$(ls ${SYSFS_BASE} | grep "10010000" | sed -n '1p')

if [ -z "$DEV" ]; then
    echo "[FAIL] Device 10010000.myled-controller not found in sysfs"
    exit 1
fi

MYLED="${SYSFS_BASE}/${DEV}/myled"
echo "=== Device: ${DEV} ==="
echo "=== sysfs path: ${MYLED} ==="
echo ""

pass() { echo "  [PASS] $1"; }
fail() { echo "  [FAIL] $1"; }

check_attr() {
    local attr=$1
    if [ -f "${MYLED}/${attr}" ]; then
        val=$(cat "${MYLED}/${attr}")
        pass "${attr} = ${val}"
    else
        fail "${attr} missing"
    fi
}

echo "── 1. Read all attributes ────────────────────────"
for attr in info enable brightness color blink status; do
    check_attr $attr
done

echo ""
echo "── 2. Set brightness = 200 ───────────────────────"
echo 200 > ${MYLED}/brightness
val=$(cat ${MYLED}/brightness)
[ "$val" = "200" ] && pass "brightness = 200" || fail "brightness mismatch: $val"

echo ""
echo "── 3. Set color = ff3300 (orange-red) ───────────"
echo ff3300 > ${MYLED}/color
val=$(cat ${MYLED}/color)
[ "$val" = "ff3300" ] && pass "color = ff3300" || fail "color mismatch: $val"

echo ""
echo "── 4. Toggle blink ON ────────────────────────────"
echo 1 > ${MYLED}/blink
val=$(cat ${MYLED}/blink)
[ "$val" = "1" ] && pass "blink = 1" || fail "blink mismatch: $val"

echo ""
echo "── 5. Disable LED ────────────────────────────────"
echo 0 > ${MYLED}/enable
val=$(cat ${MYLED}/enable)
[ "$val" = "0" ] && pass "enable = 0" || fail "enable mismatch: $val"

echo ""
echo "── 6. Final info dump ────────────────────────────"
cat ${MYLED}/info | sed 's/^/  /'

echo ""
echo "── 7. dmesg (myled related) ──────────────────────"
dmesg | grep -i myled | sed 's/^/  /'

echo ""
echo "=== Demo Complete ==="
