#!/bin/sh
# =========================================================
# myled sysfs test (100% BusyBox-safe)
# =========================================================

SYSFS_BASE="/sys/bus/platform/devices"

pass() { echo "  [PASS] $1"; }
fail() { echo "  [FAIL] $1"; }

# ---------------------------------------------------------
# Step 1: device discovery (NO basename, NO ls parsing)
# ---------------------------------------------------------
DEV_PATH=""

for d in ${SYSFS_BASE}/*; do
    # safety check
    [ -e "$d/modalias" ] || continue

    grep -q "myvendor,myled-v1" "$d/modalias" 2>/dev/null
    if [ $? -eq 0 ]; then
        DEV_PATH="$d"
        break
    fi
done

if [ -z "$DEV_PATH" ]; then
    echo "[FAIL] device not found"
    exit 1
fi

DEV=${DEV_PATH##*/}   # <<< POSIX parameter expansion (no basename)

MYLED="${DEV_PATH}/myled"

echo "=================================================="
echo " Device     : $DEV"
echo " Path       : $DEV_PATH"
echo " Sysfs node : $MYLED"
echo "=================================================="
echo ""

# ---------------------------------------------------------
# Step 2: driver check
# ---------------------------------------------------------
if [ -L "$DEV_PATH/driver" ]; then
    pass "driver bound"
else
    fail "driver NOT bound"
    echo "Hint: dmesg | grep myled"
    exit 1
fi

# ---------------------------------------------------------
# Step 3: sysfs check
# ---------------------------------------------------------
if [ ! -d "$MYLED" ]; then
    fail "sysfs missing"
    exit 1
fi

pass "sysfs ready"

# ---------------------------------------------------------
# Step 4: attribute test
# ---------------------------------------------------------
check_attr() {
    attr=$1

    if [ -f "$MYLED/$attr" ]; then
        val=$(cat "$MYLED/$attr")
        pass "$attr = $val"
    else
        fail "$attr missing"
    fi
}

echo "── read test ─────────────────────────────"

for a in info enable brightness color blink status; do
    check_attr "$a"
done

# ---------------------------------------------------------
# Step 5: write test
# ---------------------------------------------------------
echo ""
echo "── write test ─────────────────────────────"

echo 200 > "$MYLED/brightness" 2>/dev/null
echo ff3300 > "$MYLED/color" 2>/dev/null
echo 1 > "$MYLED/blink" 2>/dev/null
echo 0 > "$MYLED/enable" 2>/dev/null

echo "── done ──"

# ---------------------------------------------------------
# Step 6: dmesg
# ---------------------------------------------------------
echo ""
echo "── dmesg ──────────────────────────────────"

dmesg | grep -i myled 2>/dev/null
