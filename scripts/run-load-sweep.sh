#!/usr/bin/env bash
# run-load-sweep.sh — reproducible load sweep (M3).
#
#   Host:  ./qrtp bench --host for each load -> results/host-load-N.json
#   Guest: rebuild the generic boot disk per load (qnx_demo --load N),
#          boot it in QEMU, extract the demo JSON
#          -> qemu-boot/sweep-load-N.json (committed, like guest-result.json)
#
# Usage (from the repo root):
#   ./scripts/run-load-sweep.sh                  # loads 0 1 2 4, 2000 iters
#   ./scripts/run-load-sweep.sh --loads "1 2"     # subset of loads
#   ./scripts/run-load-sweep.sh --host-only       # skip QEMU
#   ./scripts/run-load-sweep.sh --guest-only      # skip host
#   ./scripts/run-load-sweep.sh --iterations 5000 --warmup 500
#
# Requirements: qcc/SDP for the guest part (see `qrtp doctor`); QEMU 8.2+.
# Each guest boot takes 2-3 min under TCG; the full sweep is ~15 min.
# A failed guest boot is recorded and the sweep continues with the next
# load — a missing sweep-load-N.json means that load has no data, never a
# fabricated number. Raw logs: qemu-boot/sweep-load-N.log.
# Host JSONs live in results/ (gitignored); the comparison table is
# published in docs/LOAD-SWEEP.md.
#
# After the sweep the boot disk is rebuilt from the unmodified
# qemu-boot/generic.build (load 1), so the repo is left in its canonical
# state.
set -u
cd "$(dirname "$0")/.."

LOADS="0 1 2 4"
ITER=2000
WARM=200
HOST_ONLY=0
GUEST_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --loads) LOADS="$2"; shift 2;;
        --iterations) ITER="$2"; shift 2;;
        --warmup) WARM="$2"; shift 2;;
        --host-only) HOST_ONLY=1; shift;;
        --guest-only) GUEST_ONLY=1; shift;;
        *) echo "unknown option: $1" >&2; exit 1;;
    esac
done

echo "== load sweep: loads [$LOADS], iterations $ITER, warmup $WARM =="
free -m | head -2

if [ "$GUEST_ONLY" -eq 0 ]; then
    echo "== host sweep =="
    make >/dev/null || { echo "FAIL: make"; exit 1; }
    for L in $LOADS; do
        ./qrtp bench --host --iterations "$ITER" --warmup "$WARM" \
            --load "$L" --out "results/host-load-$L.json" \
            | grep -E 'p50|p99|verdict' | sed "s/^/[host load $L] /"
    done
fi

if [ "$HOST_ONLY" -eq 0 ]; then
    echo "== guest sweep (QEMU, one boot per load) =="
    command -v qemu-system-x86_64 >/dev/null 2>&1 \
        || { echo "FAIL: qemu-system-x86_64 not found (see qrtp doctor)"; exit 1; }
    source ~/qnx/qnx800/qnxsdp-env.sh >/dev/null 2>&1 \
        || { echo "FAIL: cannot source qnxsdp-env.sh"; exit 1; }
    for L in $LOADS; do
        echo "--- guest load $L ---"
        sed -e "s/--iterations [0-9]*/--iterations $ITER/" \
            -e "s/--warmup [0-9]*/--warmup $WARM/" \
            -e "s/--load [0-9]*/--load $L/" \
            qemu-boot/generic.build > "/tmp/qnx-sweep-$L.build"
        mkifs -v "/tmp/qnx-sweep-$L.build" qemu-boot/generic.ifs >/dev/null \
            || { echo "[guest load $L] FAIL: mkifs"; continue; }
        mkqnx6fsimg qemu-boot/boot_part.build qemu-boot/boot.part >/dev/null \
            || { echo "[guest load $L] FAIL: mkqnx6fsimg"; continue; }
        BS=$(( ($(stat -c%s qemu-boot/boot.part) + 511) / 512 ))
        TOTAL=$((BS + 1))
        printf '[sector_size=512 cylinders=%s sectors_per_track=1 heads=1 start_at_cylinder=1]\n[partition=1 boot=true type=177 num_sectors=%s] "qemu-boot/boot.part"\n' "$TOTAL" "$BS" > qemu-boot/disk.layout
        diskimage -p -s 163840 -c qemu-boot/disk.layout -o qemu-boot/disk.img \
            -b "$QNX_TARGET/x86_64/boot/sys/ipl-diskpc1" >/dev/null \
            || { echo "[guest load $L] FAIL: diskimage"; continue; }
        rm -f "qemu-boot/sweep-load-$L.log" /tmp/qemu-mon-sweep.sock
        timeout 300 qemu-system-x86_64 -machine pc -cpu max -m 256 -display none \
            -serial stdio -monitor unix:/tmp/qemu-mon-sweep.sock,server,nowait \
            -net none -drive file=qemu-boot/disk.img,format=raw,if=ide \
            > "qemu-boot/sweep-load-$L.log" 2>&1
        if grep -m1 '^{"tool":"qnx-rt-probe"' "qemu-boot/sweep-load-$L.log" \
                > "qemu-boot/sweep-load-$L.json"; then
            echo "[guest load $L] JSON captured:"
            ./qrtp report "qemu-boot/sweep-load-$L.json" | sed 's/^/  /'
        else
            echo "[guest load $L] FAIL: no demo JSON in serial log (see qemu-boot/sweep-load-$L.log)"
            rm -f "qemu-boot/sweep-load-$L.json"
        fi
    done
    echo "== rebuilding canonical load-1 disk image =="
    mkifs -v qemu-boot/generic.build qemu-boot/generic.ifs >/dev/null
    mkqnx6fsimg qemu-boot/boot_part.build qemu-boot/boot.part >/dev/null
    BS=$(( ($(stat -c%s qemu-boot/boot.part) + 511) / 512 ))
    TOTAL=$((BS + 1))
    printf '[sector_size=512 cylinders=%s sectors_per_track=1 heads=1 start_at_cylinder=1]\n[partition=1 boot=true type=177 num_sectors=%s] "qemu-boot/boot.part"\n' "$TOTAL" "$BS" > qemu-boot/disk.layout
    diskimage -p -s 163840 -c qemu-boot/disk.layout -o qemu-boot/disk.img \
        -b "$QNX_TARGET/x86_64/boot/sys/ipl-diskpc1" >/dev/null
    echo "canonical disk.img rebuilt (load 1)."
fi

echo "== comparison table =="
python3 - "$LOADS" << 'EOF'
import json, sys, glob, os
def load(p):
    try:
        with open(p) as f: return json.load(f)
    except Exception:
        return None
print("| load | env   | p50 (us) | p95 (us) | p99 (us) | mean (us) | max (us) | jitter p99 (us) | verdict |")
print("|------|-------|----------|----------|----------|-----------|----------|-----------------|---------|")
for l in sys.argv[1].split():
    for env, path in (("host", f"results/host-load-{l}.json"),
                      ("guest", f"qemu-boot/sweep-load-{l}.json")):
        d = load(path)
        if not d:
            print(f"| {l} | {env} | NO DATA | | | | | | |")
            continue
        s = d["stats"]
        print(f"| {l} | {env} | {s['p50_us']:.1f} | {s['p95_us']:.1f} | {s['p99_us']:.1f} | "
              f"{s['mean_us']:.1f} | {s['max_us']:.1f} | {s['jitter_p99_us']:.1f} | {d['verdict']} |")
EOF
echo "done."
