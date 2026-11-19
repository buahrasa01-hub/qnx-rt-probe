# qnx-rt-probe

A host-side real-time smoke harness for **QNX 8.0**: a Linux CLI that cross-compiles a small C demo for QNX 8.0 with `qcc`, boots it in QEMU (QNX Quick Start Target Image), benchmarks native message-passing latency and jitter between a high-priority and a low-priority thread under load, and prints a pass/fail report with latency percentiles.

Built for the **QNX Global Hackathon 2026** (Devpost, $50,000), category **Developer Tooling & Host Tools**. The demo exercises QNX-specific technology: native message passing (`MsgSend`/`MsgReceive`), priority scheduling, and — where feasible — a resource manager.

## Status

Setup phase. QNX SDP 8.0 is installed via the QNX Everywhere free non-commercial license (see `install-sdp.sh` — requires a myQNX account + license, not yet provisioned).

## What it does

1. Cross-compile the C benchmark demo for QNX 8.0 (`qcc -Vgcc_ntox86_64`).
2. Boot the QNX 8.0 QEMU target image with the demo included.
3. Run the message-passing latency/jitter benchmark (high-priority sender vs low-priority sender under CPU load).
4. Collect the serial/console output and print a pass/fail report with p50 / p95 / p99 latency and max jitter.

## Requirements

- Linux x86_64 host (tested on Ubuntu 24.04), QEMU 8.2+
- QNX SDP 8.0 via QNX Everywhere (free non-commercial license)
- C toolchain on the host for the CLI itself

## License

MIT — see `LICENSE`.
