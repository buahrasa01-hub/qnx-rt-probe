#!/usr/bin/env bash
# run-host.sh — convenience wrapper: build, run the host benchmark with
# defaults, and re-print the report. Extra args are passed to bench.
#   scripts/run-host.sh [--iterations N] [--warmup W] [--load N] ...
set -e
cd "$(dirname "$0")/.."
make
./qrtp bench --host "$@" --out results/host-latest.json
./qrtp report results/host-latest.json
