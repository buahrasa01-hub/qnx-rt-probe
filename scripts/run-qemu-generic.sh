#!/usr/bin/env bash
# run-qemu-generic.sh — boot the generic (no-qemuvirt) QNX 8.0 image in QEMU.
# SEPARATE variant from the qemu-virt path (scripts/run-qemu.sh); the
# qemu-virt path is untouched. Verified working 20 Nov 2026 (attempt 6):
# QNX 8.0 boots, qnx_demo runs, p50 20.9 us / p99 46.2 us, verdict PASS.
#
# How it works (no QSTI, no virtio packages needed):
#   generic.build  --mkifs-->  generic.ifs (startup-x86 + procnto + demo)
#   boot_part.build --mkqnx6fsimg--> boot.part (QNX6 fs, /.boot/qnx_sdp.ifs)
#   disk.layout    --diskimage + ipl-diskpc1--> disk.img (MBR boot disk)
#   QEMU boots the disk: SeaBIOS -> IPL1 -> IPL2 -> IFS -> startup -> procnto
#
# CRITICAL: -cpu max (NOT plain qemu64).
#   - qemu64 lacks XSAVE  -> startup spins in not_supported (attempts 3-4)
#   - qemu64 lacks FSGSBASE -> procnto _start faults on `mov %rax,%cr4`
#     (attempt 5). -cpu max provides both. -cpu qemu64,+xsave,+fsgsbase
#     is the minimal equivalent.
# -kernel does NOT work for these images: mkifs output carries no multiboot
# magic, so QEMU -kernel leaves the CPU stuck in real mode (attempts 1-2).
#
# Serial console: WORKING since attempt 8 (20 Nov ~07:00 UTC).
# The image needs BOTH libsecpol.so.1 (devc-ser8250's missing library)
# AND devc-con started first with `reopen /dev/con1` before the serial
# driver — with only libsecpol (attempt 7) /dev/ser1 still never appeared
# and the serial log stayed empty. The demo result now lands in the serial
# log as text (qemu-boot/disk.log); VGA screendump is only the fallback.
#
# Usage (from the repo root):
#   ./scripts/run-qemu-generic.sh            # rebuild all + boot (300s)
#   ./scripts/run-qemu-generic.sh nobuild    # boot existing disk.img only
set -e
cd "$(dirname "$0")/.."
LOG=qemu-boot/disk.log

if [ "$1" != "nobuild" ]; then
    source ~/qnx/qnx800/qnxsdp-env.sh >/dev/null 2>&1
    echo "== mkifs =="
    mkifs -v qemu-boot/generic.build qemu-boot/generic.ifs
    echo "== mkqnx6fsimg =="
    mkqnx6fsimg qemu-boot/boot_part.build qemu-boot/boot.part
    echo "== diskimage =="
    BS=$(( ($(stat -c%s qemu-boot/boot.part) + 511) / 512 ))
    TOTAL=$((BS + 1))
    printf '[sector_size=512 cylinders=%s sectors_per_track=1 heads=1 start_at_cylinder=1]\n[partition=1 boot=true type=177 num_sectors=%s] "qemu-boot/boot.part"\n' "$TOTAL" "$BS" > qemu-boot/disk.layout
    diskimage -p -s 163840 -c qemu-boot/disk.layout -o qemu-boot/disk.img \
        -b "$QNX_TARGET/x86_64/boot/sys/ipl-diskpc1"
fi

free -m | head -2
echo "== booting (timeout 300s, log $LOG) =="
rm -f "$LOG" /tmp/qemu-mon-generic.sock
timeout 300 qemu-system-x86_64 -machine pc -cpu max -m 256 -display none \
    -serial stdio -monitor unix:/tmp/qemu-mon-generic.sock,server,nowait \
    -net none -drive file=qemu-boot/disk.img,format=raw,if=ide > "$LOG" 2>&1
echo "QEMU exited (rc=$?). Serial log:"
cat "$LOG"
echo "NOTE: the demo JSON above came over the serial log. If the log is"
echo "empty, fall back to a VGA screendump while QEMU is still running:"
echo "  QMP_SOCK=/tmp/qemu-mon-generic.sock python3 /tmp/qmp2.py 'screendump /tmp/shot.ppm'"
