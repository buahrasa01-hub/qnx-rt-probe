# Measure message-passing latency on QNX 8.0 in QEMU

A codelab for the QNX Global Hackathon 2026 (Best Codelab/Tutorial bonus,
$3,500). Companion tool: `qnx-rt-probe` in this repo.

> DRAFT — sections 1–2 and 5–6 are complete (5–6 written from two real
> guest runs, 20 Nov 2026; see `qemu-boot/BOOT-LOG.md`). Sections 3–4 and
> 7 are TODO headings for M3 (1–6 Dec); section 8 has the failures found
> so far.

## 1. Goal

By the end of this codelab you will have:

- cross-compiled a small C demo for QNX 8.0 with `qcc`,
- built a bootable disk image from that demo by hand (`mkifs` +
  `mkqnx6fsimg` + `diskimage` — no QSTI image needed) and booted it
  in QEMU,
- measured native message-passing latency (`MsgSend`/`MsgReceive`)
  between a high-priority and a low-priority thread under CPU load,
- and produced a pass/fail report with p50/p95/p99 latency and jitter.

The point is not booting QNX — boot automation already exists. The point
is **quantitative real-time measurement**: numbers that tell you whether
your priority design actually holds up under load.

## 2. Prerequisites

- A Linux x86_64 host (this codelab was written on Ubuntu 24.04).
- QEMU 8.2+ (`qemu-system-x86_64` on your PATH).
- QNX SDP 8.0 via the free QNX Everywhere non-commercial license:
  a myQNX account, the deployed license, and the Software Center
  install (`~/qnx/qnx800`, `qcc` on your PATH after sourcing
  `~/qnx/qnx800/qnxsdp-env.sh`).
- This repo: `git clone https://github.com/buahrasa01-hub/qnx-rt-probe`
  (MIT). No other dependencies: the host tool is C11 + pthreads only.
- What you will measure: round-trip time of a 64-byte ping-pong between
  two threads, in nanoseconds, with warmup excluded; percentiles p50/p95/
  p99 plus max jitter and p99 jitter; PASS when p50 ≤ 100 µs and
  p99 ≤ 500 µs (host-calibration defaults — you will re-calibrate them
  for the guest in section 6).

## 3. TODO — Check your environment (`qrtp doctor`)

One paragraph + the exact command and expected output (host-ready yes,
qnx-ready no until the SDP lands; what each line means).

## 4. TODO — Build the guest demo (`qrtp build`)

The `qcc` invocation, the target (`ntox86_64`), and how to confirm the
binary landed in the image. Include the exact build log excerpt.

## 5. Boot in QEMU and run the benchmark (`qrtp boot`, `qrtp run`)

This is the part that took six attempts to get right (full log:
`qemu-boot/BOOT-LOG.md`). The short version: QEMU cannot boot the image
with `-kernel`, and the default QEMU CPU cannot run QNX 8.0 at all. The
working path is a hand-built boot disk.

### 5.1 Build the boot disk

From the repo root (after `source ~/qnx/qnx800/qnxsdp-env.sh`):

```sh
# 1. Pack the boot image: startup + kernel + drivers + demo
mkifs -v qemu-boot/generic.build qemu-boot/generic.ifs
# 2. Put the image into a QNX6 filesystem, at /.boot/qnx_sdp.ifs
mkqnx6fsimg qemu-boot/boot_part.build qemu-boot/boot.part
# 3. Wrap it in an MBR disk with the QNX disk bootloader (IPL1)
diskimage -p -s 163840 -c qemu-boot/disk.layout -o qemu-boot/disk.img \
    -b "$QNX_TARGET/x86_64/boot/sys/ipl-diskpc1"
```

Or run `./scripts/run-qemu-generic.sh`, which does all three and boots.
What is in the image (`qemu-boot/generic.build`)? `startup-x86 -v`,
`procnto-smp-instr`, `devc-con`, `devc-ser8250`, and the compiled
`qnx_demo`. No QSTI image and no virtio packages are involved — the
installed SDP here lacks `com.qnx.qnx800.target.qemuvirt` and the
virtio disk driver, and this path does not need them.

The boot script inside the image (`.script` in `generic.build`) is:

```
display_msg "qnx-rt-probe: generic x86 boot (no qemuvirt)"
devc-con -n4
waitfor /dev/con1
reopen /dev/con1
display_msg "vga console up"
devc-ser8250 -e -F -b115200 0x3f8,4
waitfor /dev/ser1
reopen /dev/ser1
display_msg "serial console up"
display_msg "running qnx_demo"
/proc/boot/qnx_demo --iterations 2000 --warmup 200 --load 1
display_msg "qnx_demo done"
```

Two details in that script are load-bearing (see 5.4): the image must
contain `libsecpol.so.1`, and `devc-con` must start *before* the serial
driver.

### 5.2 Boot it

```sh
qemu-system-x86_64 -machine pc -cpu max -m 256 -display none \
    -serial stdio -monitor unix:/tmp/qemu-mon.sock,server,nowait \
    -net none -drive file=qemu-boot/disk.img,format=raw,if=ide
```

Boot chain: SeaBIOS → IPL1 (`ipl-diskpc1`) → IPL2
(`ipl-diskpc2-fsq6`) loads `qnx_sdp.ifs` → `startup-x86` →
`procnto-smp-instr` → the script above. The whole boot under TCG
emulation takes 2–3 minutes; be patient. A healthy boot prints this on
the serial log (real transcript, 20 Nov 2026):

```
serial console up
running qnx_demo
{"tool":"qnx-rt-probe","tool_version":"0.1.0","transport":"qnx-msgSend","host":false,"qnx":true,"iterations":2000,"warmup":200,"load_threads":1,"priority_elevated":true,"thresholds_us":{"p50":100.0,"p99":500.0,"source":"host-default"},"stats":{"count":2000,"min_us":20.4,"max_us":2052.2,"mean_us":23.5,"stddev_us":51.7,"p50_us":20.9,"p95_us":29.4,"p99_us":36.7,"jitter_max_us":2031.4,"jitter_p99_us":33.4},"verdict":"PASS"}
qnx_demo done
```

The demo's one JSON line is the report (section 6). Quit QEMU from the
monitor once it appears: `quit` on the monitor socket. If the serial log
is empty, the serial driver did not come up — read the VGA console with
a screendump instead (section 8).

### 5.3 Why `-kernel` does not work

The obvious first try — `qemu-system-x86_64 -kernel generic.ifs` —
leaves the CPU stuck in real mode (EIP `0xfa30`) with an empty serial
log. Root cause, verified: the `mkifs` output carries **no multiboot
magic** (`0x1BADB002` is absent from the whole image), so QEMU's
`-kernel` loader cannot hand it off. There is no flag to fix this; boot
from a disk image as in 5.1 instead.

### 5.4 Why `-cpu max` is mandatory (two root causes, found by disassembly)

QEMU's default CPU, `qemu64`, cannot run QNX 8.0. Two missing CPU
features, each found by reading where the CPU got stuck:

1. **XSAVE — `startup-x86` spins forever.** `startup`'s `_cstart_`
   calls `early_cpu_setup`, which runs `CPUID.1` and tests
   `ECX.XSAVE` (bit 26, `0x4000000`). `qemu64` does not expose XSAVE,
   so the test fails and `_cstart_` jumps to its `not_supported` label
   (an infinite `jmp $`). Symptom: the CPU sits at one address
   (`0x1030a4` here) forever, after IPL2 prints `Loading IFS...done`.
   Adding `+xsave` gets past this check.
2. **FSGSBASE — `procnto` faults in a loop.** Past the XSAVE check,
   the kernel's `_start` does `or $0x10000,%rax` (CR4 bit 16 =
   FSGSBASE) followed by `mov %rax,%cr4`. `qemu64` does not support
   FSGSBASE, so writing CR4 with that bit set raises `#GP`, and the
   fault loops on the same instruction. Symptom: the CPU sits at
   `0xffff8000000487ed` (procnto `_start + 0x1f`) forever, right after
   `Starting next program at ...`.

`-cpu max` enables every feature TCG supports — XSAVE, FSGSBASE, and
more — and removes the whack-a-mole. The minimal equivalent is
`-cpu qemu64,+xsave,+fsgsbase`.

### 5.5 Why the serial console needs two things

Attempt 6 booted and ran the demo, but the serial log was empty: the
boot script's `reopen /dev/ser1` failed and all output went to the VGA
console (captured by screendump). The driver had failed to load:
`ldd:FATAL: Could not load library libsecpol.so.1` — the library was
not in the image. Adding `libsecpol.so.1` to `generic.build` fixed the
load (attempt 7) but `/dev/ser1` *still* never appeared. What finally
worked (attempt 8) was starting `devc-con` and `reopen /dev/con1`
*before* the serial driver, so the boot script has a real console
device first. Rule, recorded empirically: the image needs **both**
`libsecpol.so.1` **and** the console-first order. Why the order
matters is not yet proven (a candidate is the driver's
`secpol_resmgr_attach()` call); do not rely on a mechanism nobody has
verified — just keep the order.

## 6. Read the report (`qrtp report`)

The guest prints one JSON line (section 5.2). Run it through
`qrtp report` (or read it directly). Two real runs, same image, same
flags, 20 Nov 2026:

| Run | p50 (µs) | p95 (µs) | p99 (µs) | mean (µs) | max (µs) | jitter p99 (µs) | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Attempt 6 (VGA capture) | 20.9 | 31.6 | 46.2 | 26.0 | 2740.7 | 20.7 | PASS |
| Attempt 8 (serial capture) | 20.9 | 29.4 | 36.7 | 23.5 | 2052.2 | 33.4 | PASS |

For comparison, the same benchmark on the Linux host (2000 iterations,
`bench --host`) measured **p50 15.1 µs / p99 27.8 µs**. Thresholds used
here are the host defaults: PASS when p50 ≤ 100 µs and p99 ≤ 500 µs.

What each number means:

- **p50 (median)** — the typical round-trip. 20.9 µs in both guest runs:
  the median is stable and reproducible.
- **p95 / p99** — the tail. For real-time work the tail is the number
  that matters: a control loop that is fast 95% of the time and blows
  its deadline 5% of the time is a broken loop. Note the tail moves
  run to run under emulation (p99 46.2 vs 36.7 µs); never quote a
  single run's p99 as "the" latency.
- **mean / max** — the mean hides the tail; the max (2.0–2.7 ms here)
  is a single worst excursion, useful only as a smoke signal.
- **jitter** — the largest (and p99) change between consecutive
  round-trips. p99 jitter of 20–33 µs says consecutive messages are
  usually steady; the max jitter (~2 ms) is the same one bad excursion
  as the max latency.
- **verdict** — PASS/FAIL against the thresholds in the JSON. The
  thresholds are part of the report, not of the tool: re-calibrate them
  per environment (below).

### Honest calibration: these are emulation numbers, not native-QNX numbers

QEMU here runs with TCG (software emulation, no KVM), which is roughly
**10–50× slower than native hardware**. The guest p50 of 20.9 µs is
therefore *not* what QNX 8.0 would measure on real hardware — it is
the latency of QNX 8.0 running inside an emulator, and it is only
meaningfully comparable to other numbers measured in the *same*
emulator. What the numbers do prove: the harness works end to end on a
real QNX kernel (real `MsgSend`/`MsgReceive` between real QNX threads
at elevated priority), the median is stable across runs, and the tail
stays well inside the thresholds. Do not present 20.9 µs as "QNX
message-passing latency". Present it as "QNX 8.0 under QEMU TCG on this
host, 2000 iterations, load 1 thread".

### Re-calibrating thresholds for the guest

The host defaults (p50 ≤ 100, p99 ≤ 500 µs) happen to pass here, but
they are host numbers. For your own guest runs: take 3 runs, use the
worst p50 and the worst p99, and set thresholds at ~2× those values —
tight enough to catch a real regression, loose enough that TCG noise
does not fail the build. If a run FAILs, check the tail first (section
7): a single 2 ms excursion fails nothing by itself, but a p99 that
doubled between runs means something changed.

## 7. TODO — Experiment: what changes the numbers

Suggested variations with expected direction of effect: more load
threads, tighter/looser thresholds, SCHED_FIFO vs SCHED_OTHER, and
message size. Each with a one-line `qrtp bench` command.

## 8. Troubleshooting

Failures found while writing this codelab (each one verified, not
guessed). Full attempt log: `qemu-boot/BOOT-LOG.md`.

- **QEMU `-kernel`: CPU stuck in real mode, serial empty.** The `mkifs`
  image has no multiboot magic. Do not use `-kernel`; boot from the disk
  image (section 5.1).
- **CPU stuck at one address after `Loading IFS...done`.** `startup`
  is spinning in `not_supported`: the CPU lacks XSAVE. Use `-cpu max`
  (or at least `-cpu qemu64,+xsave`).
- **CPU stuck right after `Starting next program`.** `procnto` is
  faulting on `mov %rax,%cr4` with FSGSBASE set: the CPU lacks
  FSGSBASE. Use `-cpu max` (or `-cpu qemu64,+xsave,+fsgsbase`).
- **Serial log empty, demo output only on VGA.** Two possible causes,
  in order: (1) `libsecpol.so.1` is missing from the image —
  `devc-ser8250` fails to load (`ldd:FATAL` on the VGA console); add
  the library to `generic.build` and rebuild. (2) The library is
  present but `/dev/ser1` still never appears — start `devc-con` and
  `reopen /dev/con1` *before* the serial driver in the boot script
  (section 5.5). Fallback while debugging: capture the VGA console
  with a QMP screendump (`screendump /tmp/shot.ppm` on the monitor
  socket) — the demo JSON is printed there too.
- **Boot is slow (2–3 min) or the VM seems hung.** That is normal
  under TCG. Check progress with a VGA screendump before killing
  anything; the `timeout 300` in `run-qemu-generic.sh` is there for a
  reason.
- **`priority_elevated: false` on the host.** The host benchmark needs
  elevated scheduling privileges; run it as root (or with the
  equivalent capability) or accept host numbers measured without
  elevation — and say so in the report.
- **A FAIL verdict on a slow machine.** Check the tail before blaming
  the code: one 2 ms excursion under TCG is noise, not a regression.
  Re-run twice; if p50 is stable and only p99/max move, re-calibrate
  the thresholds (section 6) instead of chasing the code.
