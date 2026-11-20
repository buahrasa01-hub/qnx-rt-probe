#!/usr/bin/env bash
# run-qemu.sh — boot the QNX Quick Start Target Image in QEMU.
#
# READY FOR M1, NOT YET RUN: no QSTI image exists on this host until the
# SDP is installed, so today this script always fails cleanly in the
# pre-flight checks below. It never boots garbage.
#
# The exact QEMU machine/cpu/kernel flags are TO BE CONFIRMED against the
# QSTI documentation at M1 (the current flags are the standard QEMU
# x86_64 guest defaults: q35 + qemu64 + -kernel <ifs>). If the QSTI docs
# prescribe different flags, update them here — not in qrtp itself.
#
# Usage: scripts/run-qemu.sh --image PATH [--memory MB] [--timeout SEC]
set -u

IMAGE=""
MEMORY=512
TIMEOUT=120

while [ $# -gt 0 ]; do
    case "$1" in
        --image)   IMAGE="$2"; shift 2 ;;
        --memory)  MEMORY="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --help)    echo "Usage: $0 --image PATH [--memory MB] [--timeout SEC]"; exit 0 ;;
        *)         echo "run-qemu.sh: unknown option: $1" >&2; exit 1 ;;
    esac
done

fail() { echo "run-qemu.sh: $*" >&2; exit 1; }

# ---- pre-flight: fail with the missing piece named ----
command -v qemu-system-x86_64 >/dev/null 2>&1 \
    || fail "qemu-system-x86_64 not found (install QEMU 8.2+)."
[ -n "$IMAGE" ] || fail "no --image given (QNX QSTI *.ifs image)."
[ -f "$IMAGE" ] || fail "image not found: $IMAGE"
[ -n "${QNX_TARGET:-}" ] || echo "run-qemu.sh: warning: QNX_TARGET unset (SDP env not sourced)." >&2

echo "run-qemu.sh: booting $IMAGE (memory ${MEMORY}M, timeout ${TIMEOUT}s)"
echo "run-qemu.sh: serial goes to stdio; the guest demo prints one JSON line."
exec timeout "$TIMEOUT" qemu-system-x86_64 \
    -machine q35 \
    -cpu qemu64 \
    -m "$MEMORY" \
    -display none \
    -serial stdio \
    -net none \
    -kernel "$IMAGE"
