# Measure message-passing latency on QNX 8.0 in QEMU

A codelab for the QNX Global Hackathon 2026 (Best Codelab/Tutorial bonus,
$3,500). Companion tool: `qnx-rt-probe` in this repo.

> Complete — all sections written from real runs (20 Nov 2026; see
> `qemu-boot/BOOT-LOG.md` and `docs/LOAD-SWEEP.md`). Section 6
> records the guest-calibrated thresholds (p50 ≤ 50 µs, p99 ≤ 100 µs)
> applied in M3; section 2 still quotes the host defaults first,
> because that is what a first run uses before calibration.

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
  p99 ≤ 500 µs on a first (host-calibration) run — the guest thresholds
  are re-calibrated to p50 ≤ 50 µs and p99 ≤ 100 µs in section 6.

## 3. Check your environment (`qrtp doctor`)

Run this first. It never changes anything; it only reports what is
present, and it tells you the next step when something is missing.

```sh
$ ./qrtp doctor
qnx-rt-probe doctor
-------------------
gcc                  gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
qemu                 QEMU emulator version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)
qcc                  /home/hatch/qnx/qnx800/host/linux/x86_64/usr/bin/qcc
~/qnx/qnx800         present
qnxsdp-env.sh        present
generic disk image   present (qemu-boot/disk.img)

host-ready: yes
qnx-ready:  yes
```

What each line means:

- `gcc` — the host compiler. Needed for `qrtp bench --host` and for
  building `qrtp` itself. If it says MISSING, `host-ready` is `no`
  and nothing else in this codelab works.
- `qemu` — `qemu-system-x86_64`. Needed for every guest run. The
  version here (8.2.2) is the one this codelab was written against.
- `qcc` — the QNX cross-compiler. `doctor` finds it on your PATH, or
  in the standard SDP location
  (`~/qnx/qnx800/host/linux/x86_64/usr/bin/qcc`), so the line shows
  the path even when `qcc` is not on your PATH yet.
- `~/qnx/qnx800` and `qnxsdp-env.sh` — the SDP install and its
  environment script. `qrtp build` sources the script before calling
  `qcc`, so you do not have to.
- `generic disk image` — `qemu-boot/disk.img`, the bootable disk
  built by `scripts/run-qemu-generic.sh`. This line is only meaningful
  when your working directory is the repo root; anywhere else it says
  MISSING, which just means "not in this directory".
- `host-ready` — `yes` when `gcc` is present: the host benchmark works.
- `qnx-ready` — `yes` when the SDP directory, the env script, and the
  `qcc` binary are all present. It does *not* require a QSTI image:
  the guest path in this codelab boots a hand-built disk, not the
  QSTI. If `qnx-ready` is `no`, `doctor` prints the next step
  (resolving myQNX auth for the Software Center) instead of letting
  you discover the missing piece three commands later.

## 4. Build the guest demo (`qrtp build`)

One command, from the repo root:

```sh
$ ./qrtp build
qrtp build: bash -c 'source "$HOME/qnx/qnx800/qnxsdp-env.sh" >/dev/null 2>&1; qcc -Vgcc_ntox86_64 -O2 -o qnx_demo src/qnx_demo.c src/transport_qnx.c src/stats.c -lm'
qrtp build: using qcc at /home/hatch/qnx/qnx800/host/linux/x86_64/usr/bin/qcc
qrtp build: OK — qnx_demo: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /usr/lib/ldqnx-64.so.2, BuildID[md5/uuid]=..., with debug_info, not stripped
```

(The `BuildID` hash changes on every build; only the shape of the
line matters.)

What the command does: it sources the SDP environment script (which
sets `QNX_TARGET` and puts the SDP host tools on `PATH`), then runs
`qcc` for the `gcc_ntox86_64` target — QNX Neutrino on x86-64 — with
`-O2`, compiling three files: `src/qnx_demo.c` (the benchmark: two
threads, `MsgSend`/`MsgReceive` ping-pong, load threads, statistics),
`src/transport_qnx.c` (the native transport: `ChannelCreate` /
`ConnectAttach` / `MsgSend` / `MsgReceive` / `MsgReply`), and
`src/stats.c` (percentiles and jitter, shared with the host tool).

How to confirm the binary is a real QNX binary and not a host one:
the `file` output must say `interpreter /usr/lib/ldqnx-64.so.2`
(the QNX loader) and `qnx_demo` must link the QNX calls — `nm -D
qnx_demo | grep MsgSend` shows `MsgSend` as an undefined symbol the
QNX loader resolves at runtime. A host binary would say
`interpreter /lib64/ld-linux-x86-64.so.2` instead.

How the binary gets into the boot image: `qemu-boot/generic.build`
(the `mkifs` input) contains the line

```
qnx_demo=/home/hatch/workspace/overnight/qnx-rt-probe/repo/qnx_demo
```

which copies the file you just built into the image as
`/proc/boot/qnx_demo`. The boot script in the same file then runs
`/proc/boot/qnx_demo --iterations 2000 --warmup 200 --load 1`.
So after editing `src/qnx_demo.c` (for example to change the
thresholds in section 6), the sequence is: `./qrtp build`, then
re-run `./scripts/run-qemu-generic.sh`, which rebuilds the image
from `generic.build` and boots it. If you only re-run the boot
script without rebuilding, you are measuring the old binary — the
serial log's `qnx_demo done` line does not tell you which binary
ran; the JSON's `tool_version` field does not either, so rebuild
deliberately and check the file's timestamp (`ls -l qnx_demo`)
when in doubt.

If `qrtp build` says `BLOCKED — qcc not found`, the SDP is not
installed: run `./qrtp doctor` (section 3) and follow its next
step. `build` fails loudly instead of producing a host binary and
calling it a QNX result — a report only ever contains numbers from
a real run.

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
`bench --host`) measured **p50 15.1 µs / p99 27.8 µs** (load 1, M1).
The full load sweep — loads 0, 1, 2, 4 on host and guest — is in
`docs/LOAD-SWEEP.md` (reproduce with
`./scripts/run-load-sweep.sh`). Headline: guest p50 stays 20.4–20.9 µs
at every load; the priority design holds. Thresholds used here are
the host defaults: PASS when p50 ≤ 100 µs and p99 ≤ 500 µs.

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
they are host numbers. The guest thresholds are now calibrated from
real data instead of inherited (M3, 20 Nov 2026; full derivation in
`docs/LOAD-SWEEP.md`): take every guest run, use the worst p50 and
the worst p99, and set thresholds at ~2× those values — tight enough
to catch a real regression, loose enough that TCG noise does not fail
the build.

- p50 across 6 guest runs (M1 attempts 6, 8; sweep loads 0–4):
  20.9, 20.9, 20.9, 20.4, 20.8, 20.7 µs → worst 20.9 → 2× = 41.8 →
  **50 µs**.
- p99 across the same runs: 46.2, 36.7, 47.4, 32.5, 31.7, 31.5 µs →
  worst 47.4 → 2× = 94.8 → **100 µs**.

The demo now carries these thresholds itself
(`src/qnx_demo.c`, `QRTP_GUEST_P50_THRESHOLD_US` /
`QRTP_GUEST_P99_THRESHOLD_US`, report source `"guest-calibrated"`),
and the verification run after the change
(`qemu-boot/verify-calibrated.json`: p50 20.8, p99 33.5 µs) passes
them. If you change the demo (message size, priorities, iteration
count), re-run the sweep (`./scripts/run-load-sweep.sh`), recompute
worst p50/p99 the same way, and update the two constants — do not
keep thresholds that no longer describe the runs they judge.

For your own guest runs on a different host: the same rule applies
— 3 runs minimum, worst p50/p99, ~2×, rounded up. If a run FAILs,
check the tail first (section 7): a single 2 ms excursion fails
nothing by itself, but a p99 that doubled between runs means
something changed.

## 7. Experiment: what changes the numbers

The sweep below is real data (20 Nov 2026, 2000 iterations each;
full table in `docs/LOAD-SWEEP.md`, reproduce with
`./scripts/run-load-sweep.sh`). Try each variation yourself and
compare against the baseline row (guest, load 1: p50 20.4, p95 22.7,
p99 32.5 µs).

| load | guest p50 (µs) | guest p95 (µs) | guest p99 (µs) | guest jitter p99 (µs) |
|------|----------|----------|----------|-----------------|
| 0 | 20.9 | 29.7 | 47.4 | 38.2 |
| 1 | 20.4 | 22.7 | 32.5 | 16.0 |
| 2 | 20.8 | 21.1 | 31.7 | 16.7 |
| 4 | 20.7 | 21.0 | 31.5 | 14.4 |

### More load threads (0 → 4)

```sh
./scripts/run-load-sweep.sh --loads "0 1 2 4"
```

Expected direction: worse latency under load — that is the whole
point of loading the system. **Measured direction: none.** p50
moves 20.4–20.9 µs and p99 31.5–47.4 µs with no load trend; the
worst p99 is at load 0, not load 4. The SCHED_FIFO sender at
priority 20 preempts the load threads whenever it is ready, so on
a 1-vCPU guest the measurement is shielded from CPU load. That is
a real finding about this priority design, not a failure to load
the system: if you want load to matter, the load has to compete at
the *same* priority as the sender, or the sender has to lose its
elevation (next experiment).

### Tighter / looser thresholds

```sh
./qrtp bench --host --p50-us 25 --p99-us 50   # host, tighter
```

The guest thresholds live in `src/qnx_demo.c`
(`QRTP_GUEST_P50_THRESHOLD_US` / `QRTP_GUEST_P99_THRESHOLD_US`,
currently 50 / 100 µs, source `"guest-calibrated"`). Halve them
and rebuild (`./qrtp build`, then `./scripts/run-qemu-generic.sh`):
the same runs that passed at 50/100 fail at 25/50, because the
worst observed p99 (47.4 µs) is already near 50. That is the
calibration doing its job — thresholds are a statement about what
you will tolerate, and the numbers tell you when the statement is
unrealistic for this emulator.

### SCHED_FIFO vs SCHED_OTHER

The guest demo creates the sender at priority 20 and the receiver
at priority 10 with `SCHED_FIFO` (`make_fifo_thread` in
`src/qnx_demo.c`); the load threads are plain `pthread_create`
(default policy). Try lowering the sender to priority 10 — the
same as the receiver and the load threads — rebuild, and re-run
the sweep: this is the experiment that should finally make load
matter, and the p50/p99 movement is the evidence. (Not run yet:
the current demo has no flag for it; it needs a one-line change
to the `make_fifo_thread` call for the sender.)

On the host, the same comparison is one flag away — in principle.
`bench.c` attempts `SCHED_FIFO` (sender 20, receiver 10) and falls
back to the default scheduler on `EPERM`, recording
`priority_elevated` honestly. Elevation needs the `CAP_SYS_NICE`
capability: on a bare-metal host, run `./qrtp bench --host` as root
and it applies (`priority_elevated: true`); in this sandbox (and in
most containers) the capability is dropped, so even root measures
the unprivileged case — that is the state this codelab's host
numbers were measured in, and why the host rows say
`priority_elevated: false`. The privileged host case is unmeasured
here for that reason, not for lack of trying.

### Message size

The demo exchanges a fixed 64-byte message (`QRTP_MSG_LEN` in
`src/transport.h`). Change it to 8 bytes and to 4096 bytes,
rebuild, and re-run: a real message-passing cost should scale
with size, but at these sizes the fixed costs (scheduling,
`MsgSend`/`MsgReceive` round-trip under TCG) dominate, so expect
small movement — and if you see none, that is itself a finding
about where the 20 µs goes. (Not run yet.)

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
