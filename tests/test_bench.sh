#!/usr/bin/env bash
# test_bench.sh — smoke test for the host path (also run by `make test`).
# Builds, runs doctor, runs a small host bench, re-prints the report,
# and asserts the QNX stubs fail cleanly. Exits non-zero on any failure.
set -u
cd "$(dirname "$0")/.."

echo "== smoke: make =="
make || { echo "FAIL: make"; exit 1; }

echo "== smoke: doctor =="
./qrtp doctor || { echo "FAIL: doctor"; exit 1; }

echo "== smoke: bench 1000/100 =="
./qrtp bench --host --iterations 1000 --warmup 100 \
    --out examples/host-report-example.json || { echo "FAIL: bench"; exit 1; }
python3 -c "import json; d=json.load(open('examples/host-report-example.json')); assert 'verdict' in d, 'missing verdict'; print('report JSON ok, verdict:', d['verdict'])"
python3 -m json.tool examples/host-report-example.json >/dev/null \
    || { echo "FAIL: report JSON does not parse"; exit 1; }

echo "== smoke: report =="
./qrtp report examples/host-report-example.json || { echo "FAIL: report"; exit 1; }

echo "== smoke: build stub must fail =="
if ./qrtp build; then echo "FAIL: build stub should exit non-zero"; exit 1; fi
echo "ok: build stub fails as expected"

echo "== smoke: boot stub must fail =="
if ./qrtp boot; then echo "FAIL: boot stub should exit non-zero"; exit 1; fi
echo "ok: boot stub fails as expected"

echo "== all smoke checks passed =="
