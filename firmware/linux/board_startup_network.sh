#!/bin/bash

ip link set eth0 up
ip addr replace 192.168.137.100/24 dev eth0
ip route replace 192.168.137.0/24 dev eth0

# Fan-at-100% baseline: warning 3000, critical 4500.
pl_regs=/root/fpga_ps/pl_regs
if [ -x "$pl_regs" ]; then
    PL_REG_BASE=0x80000000 "$pl_regs" write 0x80 3000 >/dev/null
    PL_REG_BASE=0x80000000 "$pl_regs" write 0x84 4500 >/dev/null
fi

exit 0
