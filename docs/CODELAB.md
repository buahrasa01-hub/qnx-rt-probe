# Measure message-passing latency on QNX 8.0 in QEMU

A codelab for the QNX Global Hackathon 2026 (Best Codelab/Tutorial bonus,
$3,500). Companion tool: `qnx-rt-probe` in this repo.

> DRAFT — sections 1–2 are complete. Sections 3–8 are TODO headings for
> M3 (1–6 Dec), to be finished once the first QNX guest run produces real
> numbers.

## 1. Goal

By the end of this codelab you will have:

- cross-compiled a small C demo for QNX 8.0 with `qcc`,
- booted it in QEMU from the QNX Quick Start Target Image (QSTI),
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
  install (`~/qnx800`, `qcc` on your PATH after sourcing
  `~/qnx800/qnxsdp-env.sh`).
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

## 5. TODO — Boot in QEMU and run the benchmark (`qrtp boot`, `qrtp run`)

The QEMU flags, serial output, what a healthy boot looks like, and the
one JSON line the guest prints. Include a real serial transcript.

## 6. TODO — Read the report (`qrtp report`)

Annotated real report: what each number means, why p99 matters more
than the mean for real-time work, and how to re-calibrate thresholds
for the guest (host defaults are NOT guest thresholds).

## 7. TODO — Experiment: what changes the numbers

Suggested variations with expected direction of effect: more load
threads, tighter/looser thresholds, SCHED_FIFO vs SCHED_OTHER, and
message size. Each with a one-line `qrtp bench` command.

## 8. TODO — Troubleshooting

Common failures and fixes: SDP auth, QEMU flags, no serial output,
`priority_elevated: false` on the host, and a FAIL verdict on a slow
machine.
