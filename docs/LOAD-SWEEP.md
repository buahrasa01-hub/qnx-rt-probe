# Load sweep — 20 Nov 2026 (M3)

Reproducible with `./scripts/run-load-sweep.sh` (loads 0, 1, 2, 4;
2000 measured iterations, 200 warmup excluded, 64-byte ping-pong).
Host runs: `./qrtp bench --host` (POSIX socketpair, unprivileged —
`priority_elevated: false`, SCHED_OTHER). Guest runs: `qnx_demo` in
QEMU (`-cpu max`, `-m 256`, TCG, 1 vCPU) with a SCHED_FIFO sender at
priority 20 and receiver at priority 10, busy-loop load threads.
Raw data: `results/host-load-N.json` (local, gitignored) and
`qemu-boot/sweep-load-N.json` (committed). M1 guest runs at load 1:
`qemu-boot/guest-result.json` (attempt 6, VGA capture) and
`qemu-boot/serial-demo-result.json` (attempt 8, serial capture).

## Results

| load | env | p50 (µs) | p95 (µs) | p99 (µs) | mean (µs) | max (µs) | jitter p99 (µs) | verdict |
|------|-----|----------|----------|----------|-----------|----------|-----------------|---------|
| 0 | host | 14.8 | 20.3 | 94.1 | 16.0 | 1296.4 | 116.7 | PASS |
| 0 | guest | 20.9 | 29.7 | 47.4 | 23.6 | 2081.9 | 38.2 | PASS |
| 1 | host | 14.5 | 16.6 | 30.5 | 15.3 | 1428.3 | 23.8 | PASS |
| 1 | guest | 20.4 | 22.7 | 32.5 | 22.5 | 1983.5 | 16.0 | PASS |
| 2 | host | 10.1 | 15.2 | 24.0 | 10.5 | 70.0 | 17.5 | PASS |
| 2 | guest | 20.8 | 21.1 | 31.7 | 22.3 | 1857.6 | 16.7 | PASS |
| 4 | host | 10.1 | 10.8 | 21.3 | 44.5 | 62412.3 | 21.9 | PASS |
| 4 | guest | 20.7 | 21.0 | 31.5 | 22.5 | 1900.4 | 14.4 | PASS |

All verdicts use the host defaults (p50 ≤ 100 µs, p99 ≤ 500 µs) —
the guest-calibrated thresholds (p50 ≤ 50 µs, p99 ≤ 100 µs, see
below) were applied to the demo only after this sweep; every run
above also passes them.

## What the guest numbers say

**The priority design holds under load — that is the headline.**
Guest p50 is 20.4–20.9 µs at every load from 0 to 4 busy-loop
threads on a single vCPU: the median does not move. Guest p99 is
31.5–47.4 µs with no load trend (the worst, 47.4 µs, is at load 0,
not load 4 — it is TCG tail noise, not load). Jitter p99 is
14–38 µs. The SCHED_FIFO sender at priority 20 is effectively
shielded from the load threads: when the sender is ready, it runs,
and the load threads — whatever their number — wait.

Two caveats, both honest:

- **These are emulation numbers.** QEMU TCG is roughly 10–50×
  slower than native hardware. 20.7 µs is not "QNX message-passing
  latency"; it is "QNX 8.0 under QEMU TCG on this host, 2000
  iterations". Only compare numbers measured in the same emulator.
- **The tail moves run to run.** Across the three load-1 guest runs
  (M1 attempts 6 and 8 plus this sweep) p99 was 46.2, 36.7, and
  32.5 µs while p50 stayed 20.4–20.9 µs. Never quote a single run's
  p99 as "the" latency; quote p50/p95/p99 together.

The ~2 ms `max` in every guest run is a single worst excursion
(one sample in 2000); it is a smoke signal, not the story. The
same applies to `jitter max` (~1.8–2.1 ms).

## What the host numbers say (and do not say)

- Host p50 *falls* with load (14.8 → 10.1 µs from load 0 to load 4).
  That is counter-intuitive and is **not** claimed as a real
  improvement: the likely cause is host CPU frequency scaling /
  scheduling on a busy shared VM, not the benchmark getting faster
  under load. Do not cite it as a result.
- Host load 0 has an outlier p99 (94.1 µs) and jitter p99
  (116.7 µs) against 21–31 µs at loads 1–4: host noise on a shared
  VM, same class as the guest's TCG tail noise.
- Host load 4 has a 62.4 ms `max` (mean 44.5 µs): a single host
  scheduling stall. p99 (21.3 µs) is unaffected — another case of
  the max being a smoke signal.

## Guest threshold calibration (applied in M3)

Rule (docs/CODELAB.md section 6): 2× the worst p50 / worst p99
across all guest runs, rounded up to a round number.

- p50 across 6 guest runs (M1 attempts 6, 8; sweep loads 0–4):
  20.9, 20.9, 20.9, 20.4, 20.8, 20.7 → worst 20.9 → 2× = 41.8 →
  **50 µs**.
- p99 across the same runs: 46.2, 36.7, 47.4, 32.5, 31.7, 31.5 →
  worst 47.4 → 2× = 94.8 → **100 µs**.

Implemented in `src/qnx_demo.c` (`QRTP_GUEST_P50_THRESHOLD_US`,
`QRTP_GUEST_P99_THRESHOLD_US`, report source `"guest-calibrated"`).
Every guest run so far passes with ~2× margin; a 2.4× rise in p50
or a 2.1× rise in p99 fails the build. The host defaults (100/500)
remain host-only and are labelled `"host-default"`.

Verification run (20 Nov 2026, after the change, load 1):
`qemu-boot/verify-calibrated.json` — p50 20.8 µs, p95 25.2 µs,
p99 33.5 µs, jitter p99 29.5 µs, verdict PASS against the new
50/100 µs thresholds. That is the 7th guest run; all 7 pass the
calibrated thresholds.
