/*
 * main.c — qnx-rt-probe CLI.
 *
 * Subcommands:
 *   doctor   environment check (never fails the build)
 *   bench    host benchmark (only --host works today)
 *   report   re-print a report JSON
 *   build    STUB: cross-compile the QNX demo (needs qcc / SDP)
 *   boot     STUB: boot the QNX image in QEMU (needs a QSTI image)
 *   run      STUB: full pipeline (blocked on the SDP)
 *
 * Stubs fail loudly with the missing piece named. Nothing here fakes a
 * QNX result: a report only ever contains numbers from a real run.
 */
#define _POSIX_C_SOURCE 200809L

#include "bench.h"
#include "report.h"

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void usage(FILE *out) {
    fprintf(out,
        "Usage: qrtp <command> [options]\n"
        "\n"
        "Commands:\n"
        "  doctor            Check the host / QNX environment\n"
        "  bench --host      Run the host message-passing benchmark\n"
        "  report <file>     Re-print a report JSON\n"
        "  build             Cross-compile the QNX demo (needs qcc; STUB)\n"
        "  boot [--image P]  Boot the QNX image in QEMU (needs QSTI; STUB)\n"
        "  run               Full pipeline build+boot+bench (STUB)\n"
        "\n"
        "Run `qrtp <command> --help` for per-command options.\n");
}

static void bench_usage(FILE *out) {
    fprintf(out,
        "Usage: qrtp bench --host [--iterations N] [--warmup W] [--load N]\n"
        "                     [--out FILE] [--p50-us X] [--p99-us Y]\n"
        "\n"
        "  --host            Run on the host (POSIX socketpair transport).\n"
        "                    The QNX guest path needs the SDP (see qrtp build).\n"
        "  --iterations N    Measured iterations (default 10000)\n"
        "  --warmup W        Warmup iterations, excluded (default 500)\n"
        "  --load N          Busy-loop CPU-load threads (default 1)\n"
        "  --out FILE        JSON report path (default results/<timestamp>.json)\n"
        "  --p50-us X        PASS threshold for p50 in us (default 100)\n"
        "  --p99-us Y        PASS threshold for p99 in us (default 500)\n"
        "\n"
        "Thresholds are host-calibration placeholders, labelled as such in\n"
        "the report. Exit status is 0 when the run completed; the verdict\n"
        "(PASS/FAIL) is data in the report, not the exit code.\n");
}

/* Run a command and capture its first output line (or NULL). */
static char *first_line(const char *cmd) {
    FILE *p = popen(cmd, "r");
    if (!p) return NULL;
    static char buf[256];
    if (!fgets(buf, sizeof(buf), p)) {
        pclose(p);
        return NULL;
    }
    pclose(p);
    buf[strcspn(buf, "\r\n")] = '\0';
    return buf;
}

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int qnx800_has_ifs(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d)) != NULL) {
        const char *n = e->d_name;
        size_t len = strlen(n);
        if (len > 4 && strcmp(n + len - 4, ".ifs") == 0) { found = 1; break; }
    }
    closedir(d);
    return found;
}

static int cmd_doctor(void) {
    printf("qnx-rt-probe doctor\n");
    printf("-------------------\n");
    char *gcc_v = first_line("gcc --version 2>/dev/null | head -1");
    printf("gcc                  %s\n", gcc_v ? gcc_v : "MISSING");
    char *qemu_v = first_line("qemu-system-x86_64 --version 2>/dev/null | head -1");
    printf("qemu                 %s\n", qemu_v ? qemu_v : "MISSING");
    char *qcc = first_line("command -v qcc 2>/dev/null");
    printf("qcc                  %s\n", qcc ? qcc : "MISSING (QNX SDP 8.0 not installed)");
    const char *home = getenv("HOME");
    char qnx800[PATH_MAX] = "";
    if (home) snprintf(qnx800, sizeof(qnx800), "%s/qnx800", home);
    printf("~/qnx800            %s\n",
           (qnx800[0] && path_exists(qnx800)) ? "present" : "MISSING");
    const char *target = getenv("QNX_TARGET");
    printf("QNX_TARGET           %s\n", target ? target : "(unset)");
    printf("QSTI image (*.ifs)   %s\n",
           (qnx800[0] && qnx800_has_ifs(qnx800)) ? "present" : "MISSING");
    printf("\n");
    printf("host-ready: %s\n", gcc_v ? "yes" : "no");
    printf("qnx-ready:  no — SDP not installed (qcc missing, no QSTI image)\n");
    printf("next step:  resolve myQNX auth for the Software Center, then run\n");
    printf("            install-sdp.sh (see PLAN.md, risk R8).\n");
    return 0;
}

static int parse_int(const char *s, const char *flag, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0' || v < 0 || v > 100000000) {
        fprintf(stderr, "qrtp: invalid value for %s: '%s'\n", flag, s);
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int parse_double(const char *s, const char *flag, double *out) {
    char *end = NULL;
    double v = strtod(s, &end);
    if (!end || *end != '\0' || v < 0) {
        fprintf(stderr, "qrtp: invalid value for %s: '%s'\n", flag, s);
        return -1;
    }
    *out = v;
    return 0;
}

/* mkdir -p for the directory part of path (no-op if none). */
static void make_parent_dir(const char *path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static int cmd_bench(int argc, char **argv) {
    qrtp_bench_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.iterations = 10000;
    opts.warmup = 500;
    opts.load_threads = 1;
    opts.p50_threshold_us = 100.0;
    opts.p99_threshold_us = 500.0;
    const char *out = NULL;
    int host = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) { bench_usage(stdout); return 0; }
        if (strcmp(argv[i], "--host") == 0) { host = 1; continue; }
        if (strcmp(argv[i], "--qnx") == 0) {
            fprintf(stderr, "qrtp: QNX guest bench needs the SDP (qcc + QSTI image).\n"
                            "  See `qrtp build` — today only `bench --host` works.\n");
            return 1;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "qrtp: option %s needs a value\n", argv[i]);
            return 1;
        }
        if (strcmp(argv[i], "--iterations") == 0) {
            if (parse_int(argv[++i], "--iterations", &opts.iterations) != 0) return 1;
        } else if (strcmp(argv[i], "--warmup") == 0) {
            if (parse_int(argv[++i], "--warmup", &opts.warmup) != 0) return 1;
        } else if (strcmp(argv[i], "--load") == 0) {
            if (parse_int(argv[++i], "--load", &opts.load_threads) != 0) return 1;
        } else if (strcmp(argv[i], "--out") == 0) {
            out = argv[++i];
        } else if (strcmp(argv[i], "--p50-us") == 0) {
            if (parse_double(argv[++i], "--p50-us", &opts.p50_threshold_us) != 0) return 1;
        } else if (strcmp(argv[i], "--p99-us") == 0) {
            if (parse_double(argv[++i], "--p99-us", &opts.p99_threshold_us) != 0) return 1;
        } else {
            fprintf(stderr, "qrtp: unknown bench option: %s\n", argv[i]);
            bench_usage(stderr);
            return 1;
        }
    }
    if (!host) {
        fprintf(stderr, "qrtp: bench needs --host (QNX guest bench is not available yet).\n");
        bench_usage(stderr);
        return 1;
    }
    if (opts.iterations <= 0) {
        fprintf(stderr, "qrtp: --iterations must be > 0\n");
        return 1;
    }

    char outbuf[PATH_MAX];
    if (!out) {
        time_t now = time(NULL);
        struct tm tmv;
        gmtime_r(&now, &tmv);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y%m%dT%H%M%SZ", &tmv);
        snprintf(outbuf, sizeof(outbuf), "results/%s.json", ts);
        out = outbuf;
    }
    make_parent_dir(out);

    qrtp_bench_result_t res;
    if (qrtp_bench_run(&opts, &res) != 0) {
        fprintf(stderr, "qrtp: benchmark run failed (transport or thread error)\n");
        return 1;
    }
    const char *verdict = qrtp_verdict(res.stats.p50_us, res.stats.p99_us,
                                       opts.p50_threshold_us, opts.p99_threshold_us);
    if (qrtp_report_write(out, &opts, &res, verdict) != 0) {
        fprintf(stderr, "qrtp: cannot write report to %s: %s\n", out, strerror(errno));
        return 1;
    }
    qrtp_report_print(&opts, &res, verdict);
    printf("report written: %s\n", out);
    return 0;
}

static int cmd_report(int argc, char **argv) {
    if (argc != 1 || strcmp(argv[0], "--help") == 0) {
        fprintf(stderr, "Usage: qrtp report <file>\n");
        return 1;
    }
    qrtp_bench_opts_t opts;
    qrtp_bench_result_t res;
    char verdict[16];
    if (qrtp_report_read(argv[0], &opts, &res, verdict, sizeof(verdict)) != 0) {
        fprintf(stderr, "qrtp: cannot read report '%s' (missing file or malformed/not a qrtp report)\n",
                argv[0]);
        return 1;
    }
    qrtp_report_print(&opts, &res, verdict);
    return 0;
}

static int cmd_build(void) {
    char *qcc = first_line("command -v qcc 2>/dev/null");
    if (!qcc) {
        fprintf(stderr,
            "qrtp build: BLOCKED — qcc not found.\n"
            "  Missing: QNX SDP 8.0 (qcc cross-compiler) and the QNX_TARGET env.\n"
            "  When present, this command cross-compiles src/qnx_demo.c for\n"
            "  ntox86_64 (qcc -Vgcc_ntox86_64).\n"
            "  Next step: resolve myQNX auth for the Software Center, then run\n"
            "  install-sdp.sh (see PLAN.md, risk R8).\n");
        return 1;
    }
    fprintf(stderr,
        "qrtp build: qcc found at %s, but the cross-compile wiring is not\n"
        "  implemented yet (lands with M1, once the SDP is installed).\n", qcc);
    return 1;
}

static int cmd_boot(int argc, char **argv) {
    const char *image = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "Usage: qrtp boot [--image PATH]\n");
            return 1;
        }
        if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            image = argv[++i];
        } else {
            fprintf(stderr, "qrtp: unknown boot option: %s\n", argv[i]);
            return 1;
        }
    }
    if (!image) {
        fprintf(stderr,
            "qrtp boot: BLOCKED — no QSTI image given and none found.\n"
            "  Missing: QNX Quick Start Target Image (*.ifs) from the SDP.\n"
            "  Usage: qrtp boot --image <path-to-ifs>\n"
            "  Next step: install the SDP (see `qrtp build`).\n");
        return 1;
    }
    if (!path_exists(image)) {
        fprintf(stderr, "qrtp boot: image not found: %s\n", image);
        return 1;
    }
    /* Hand off to the orchestration script (replaces this process). */
    execlp("scripts/run-qemu.sh", "run-qemu.sh", "--image", image, (char *)NULL);
    fprintf(stderr, "qrtp boot: cannot run scripts/run-qemu.sh: %s\n", strerror(errno));
    return 1;
}

static int cmd_run(void) {
    fprintf(stderr,
        "qrtp run: BLOCKED — the full pipeline (build -> boot -> bench in\n"
        "  the QNX guest -> report) needs the SDP (qcc + QSTI image).\n"
        "  Working path today: `qrtp bench --host` (host measurement).\n"
        "  Next step: resolve myQNX auth, run install-sdp.sh (PLAN.md, R8).\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 1;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "doctor") == 0) return cmd_doctor();
    if (strcmp(cmd, "bench") == 0) return cmd_bench(argc - 2, argv + 2);
    if (strcmp(cmd, "report") == 0) return cmd_report(argc - 2, argv + 2);
    if (strcmp(cmd, "build") == 0) return cmd_build();
    if (strcmp(cmd, "boot") == 0) return cmd_boot(argc - 2, argv + 2);
    if (strcmp(cmd, "run") == 0) return cmd_run();
    fprintf(stderr, "qrtp: unknown command: %s\n", cmd);
    usage(stderr);
    return 1;
}
