#!/bin/sh
# IF-01 PL 验收 — 在板子 root 下运行
set -e
BASE="${PL_REG_BASE:-0x80000000}"
PL_REGS="${PL_REGS:-./pl_regs}"
MOTOR="${MOTOR:-./motor_ctrl}"

fail() { echo "FAIL: $*"; exit 1; }
ok()   { echo "OK: $*"; }

echo "=== IF-01 accept base=$BASE ==="

v08=$($PL_REGS read 0x08 | awk '{print $NF}')
v30=$($PL_REGS read 0x30 | awk '{print $NF}')
v04=$($PL_REGS read 0x04 | awk '{print $NF}')

echo "0x08 SAMPLE_RATE read = $v08 (expect 3735928559 = 0xDEADBEEF)"
echo "0x30 VERSION       = $v30 (expect 65537 = 0x00010001)"
echo "0x04 STATUS        = $v04 (bit4 PL ready)"

[ "$v08" = "3735928559" ] || [ "$v08" = "0xDEADBEEF" ] || fail "0x08 magic"
[ "$v30" = "65537" ] || [ "$v30" = "0x00010001" ] || fail "VERSION"

$MOTOR run 1 500 0 3
$MOTOR run 2 500 0 3
$MOTOR stop all

ok "IF-01 PL + motor register path"
