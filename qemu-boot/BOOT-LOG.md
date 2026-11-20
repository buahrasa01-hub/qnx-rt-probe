# QEMU boot log — QNX 8.0 (SDP 8.0, ~/qnx/qnx800) on QEMU 8.2.2 TCG, no KVM
# Repo: ~/workspace/overnight/qnx-rt-probe/repo
# All attempts: -machine pc, -m 256, -display none, -serial stdio, -net none
# Image: qemu-boot/generic.ifs (mkifs, startup-x86 + procnto-smp-instr +
#        devc-ser8250 + qnx_demo), boot via disk image (diskimage + IPL1
#        ipl-diskpc1 + IPL2 ipl-diskpc2-fsq6, QNX6 fs with /.boot/qnx_sdp.ifs)

## Attempt 1 — QEMU -kernel (q35), compressed IFS
- rc=124 (timeout 180s), serial EMPTY. CPU stuck real-mode EIP=0000fa30.
- Root cause: IFS has NO multiboot magic (0x1BADB002 absent in whole 1.3 MB
  file); -kernel cannot boot it.

## Attempt 2 — QEMU -kernel (pc), monitor socket
- Same: serial empty, CPU stuck real-mode, VGA = garbage.
- Root cause: same as attempt 1.

## Attempt 3 — disk boot, compressed IFS (1,304,512 B), -cpu qemu64
- SeaBIOS -> IPL1 -> IPL2 loaded qnx_sdp.ifs (VGA: "QNX v1.2d Boot Loader:
  qnx_sdp.ifs ."). CPU entered 64-bit mode, RIP stuck at 0x1030a4 for 240s.
- 0x1030a4 = startup _cstart_ + 0x8c = label `not_supported` (jmp $).
- Root cause (disassembly of startup-x86): _cstart_ calls early_cpu_setup,
  which does CPUID.1 and tests ECX.XSAVE (bit 26, 0x4000000). qemu64 does
  NOT expose XSAVE -> returns 0 -> _cstart_ spins in not_supported.

## Attempt 4 — disk boot, UNCOMPRESSED IFS (2,576,452 B) + startup-x86 -v,
#            -cpu qemu64
- Same stuck RIP 0x1030a4. Compression was NOT the cause. Serial empty,
  VGA showed only the IPL2 line (startup never printed -v output).

## Attempt 5 — disk boot, uncompressed IFS, -cpu qemu64,+xsave
- BREAKTHROUGH: startup passed the XSAVE check. VGA shows full -v output
  ("Loading IFS...done", "System page at phys:...3c8000",
  "Starting next program at vfff8000000487ce"). CPU entered procnto
  (RIP 0xffff8000000487ed, CS=0x20, CR3=0x3bd000).
- STALL: RIP stuck at 0xffff8000000487ed for 90+s, serial empty, VGA frozen
  after "Starting next program".
- 0x...487ed = procnto _start + 0x1f = instruction `mov %rax,%cr4` at
  0x476cd, right after `or $0x10000,%rax` (CR4 bit 16 = FSGSBASE).
- Root cause: qemu64 does NOT support FSGSBASE -> writing CR4 with bit 16
  set raises #GP -> fault loop at the same instruction (no triple fault /
  no reboot observed; RIP stays on the faulting instruction).

## Attempt 6 (next) — disk boot, uncompressed IFS, -cpu max
- -cpu max enables all TCG-supported features (XSAVE + FSGSBASE + more),
  removing the whack-a-mole with individual flags. If procnto stalls again
  on another missing feature, the new RIP will identify it.

## Attempt 6 — disk boot, uncompressed IFS, -cpu max — SUCCESS (20 Nov ~06:52 UTC)
- Command: qemu-system-x86_64 -machine pc -cpu max -m 256 -display none
  -serial stdio -monitor unix:/tmp/qemu-mon4.sock,server,nowait -net none
  -drive file=qemu-boot/disk.img,format=raw,if=ide (timeout 300s).
- CPU passed procnto _start (CR4 now 0x506f0 incl. FSGSBASE+OSXSAVE),
  kernel running (RIP 0xffff800000048ef1, HLT=1 idle).
- VGA shows the full boot + demo run. Serial log stayed EMPTY (0 bytes):
  devc-ser8250 failed to load — "ldd:FATAL: Could not load library
  libsecpol.so.1" (not in the IFS) — so /dev/ser1 never existed, the
  script's `reopen /dev/ser1` failed, and all output went to VGA.
- DEMO RESULT (transcribed from VGA screendump qemu-boot/vga-demo-result.png,
  verified by 2x enlarged crop; also saved as qemu-boot/guest-result.json):
    {"tool":"qnx-rt-probe","tool_version":"0.1.0","transport":"qnx-msgSend",
     "host":"false","qnx":true,"iterations":2000,"warmup":200,
     "load_threads":1,"priority_elevated":true,
     "thresholds_us":{"p50":100.0,"p99":500.0},"source":"host-default",
     "stats":{"count":2000,"min_us":20.4,"max_us":2740.7,"mean_us":26.0,
     "stddev_us":61.1,"p50_us":20.9,"p95_us":31.6,"p99_us":46.2,
     "jitter_max_us":2709.1,"jitter_p99_us":20.7},"verdict":"PASS"}
  - M1 DELIVERABLE: p50 20.9 us, p99 46.2 us — PASS vs thresholds p50<100 / p99<500.
  - Note: p50/p99 here are GUEST MsgSend round-trip latencies under QEMU TCG
    (emulated, ~10-50x slower than native). Host bench was p50 15.1 / p99 27.8 us.
    The guest numbers calibrate the harness, they are NOT native-QNX numbers.
- scripts/run-qemu-generic.sh rewritten to this working disk-boot path
  (was the broken -kernel variant). QEMU quit cleanly via monitor after capture.

## Follow-ups (not blockers for M1)
- Add libsecpol.so.1 to generic.build so devc-ser8250 loads and the serial
  console works (output to a text log instead of VGA screenshots).
- The demo ran with load_threads=1; CODELAB part 3 can now be written from
  these numbers + BOOT-LOG.md.
- No Simular Box install needed: the 2 missing packages (qemuvirt, virtio
  devb) were never required for this path.

## Attempt 7 — disk boot, uncompressed IFS + libsecpol.so.1, -cpu max
   (20 Nov ~06:54 UTC) — SERIAL STILL DEAD
- generic.build gained `libsecpol.so.1` (dumpifs-verified present in the
  IFS at proc/boot/libsecpol.so.1, 0x6000 B). Rebuilt IFS (2,601,028 B)
  + disk image, booted with the same QEMU flags.
- Result: demo ran again (new numbers on VGA: p50 20.6 / p95 21.0 /
  p99 30.6, PASS) but serial log stayed 0 bytes. VGA showed
  `Unable to access "/dev/ser1" (2)` twice (from `waitfor` + `reopen`):
  /dev/ser1 never appeared, so output went to VGA again.
- Diagnostic notes: the `ldd:FATAL libsecpol.so.1` error from attempt 6
  did NOT reappear, so the driver loaded this time — but it created no
  device and printed no error. The driver's own stderr is invisible in
  this image (no /dev/console exists; only the kernel's loader messages
  and display_msg reach the VGA debug channel), so the exact in-driver
  failure could not be seen. devc-ser8250 also calls
  secpol_resmgr_attach() (seen in its strings), a second suspect.
- Conclusion: libsecpol.so.1 was NECESSARY (attempt 6 proved the load
  failure) but NOT SUFFICIENT for a working serial console.

## Attempt 8 — disk boot, IFS + libsecpol.so.1 + devc-con, -cpu max
   (20 Nov ~07:00 UTC) — SERIAL WORKS
- Change: added `devc-con` to generic.build and, in the .script, start
  `devc-con -n4` + `waitfor /dev/con1` + `reopen /dev/con1` BEFORE
  devc-ser8250. Rationale: give the boot script a real console device
  first, so driver stdio/stderr has somewhere to go.
- Result: SERIAL LOG CAPTURED (qemu-boot/serial-demo-result.log, 478 B):
    serial console up
    running qnx_demo
    {"tool":"qnx-rt-probe","tool_version":"0.1.0","transport":"qnx-msgSend",
     "host":false,"qnx":true,"iterations":2000,"warmup":200,
     "load_threads":1,"priority_elevated":true,
     "thresholds_us":{"p50":100.0,"p99":500.0},"source":"host-default",
     "stats":{"count":2000,"min_us":20.4,"max_us":2052.2,"mean_us":23.5,
     "stddev_us":51.7,"p50_us":20.9,"p95_us":29.4,"p99_us":36.7,
     "jitter_max_us":2031.4,"jitter_p99_us":33.4},"verdict":"PASS"}
    qnx_demo done
  (Same JSON saved as qemu-boot/serial-demo-result.json.)
- So the working recipe is libsecpol.so.1 AND devc-con + reopen /dev/con1
  before the serial driver. WHY the console-first order matters is
  UNVERIFIED (candidate: the driver's secpol_resmgr_attach / stdio setup
  needs /dev/console to exist); the empirical rule is recorded, the
  mechanism is not claimed.
- Reproducibility note: p50 is 20.9 us in BOTH guest runs (attempt 6 and
  8); p99 varies run to run under TCG (46.2 vs 36.7 us). TCG numbers are
  noisy at the tail; report p50/p95/p99 together, never a single run's p99.
- scripts/run-qemu-generic.sh comment updated: serial is the working
  capture path now; VGA screendump is the fallback.
