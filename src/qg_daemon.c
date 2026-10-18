/*
 * qg_daemon.c - Xiaomi Redmi Note 9 Pro / Mi 10T Lite (gauguin) Open-Source Battery Daemon
 * Fixes Qualcomm Fuel Gauge (QG) desynchronization and freezing percentage on Custom ROMs.
 *
 * Released under the MIT License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#define LOG_FILE "/data/local/tmp/qg-daemon.log"
#define QG_DEV   "/dev/qg"
#define FG_RESET "/sys/class/power_supply/bms/fg_reset"
#define CAPACITY "/sys/class/power_supply/bms/capacity"
#define VOLT_NOW "/sys/class/power_supply/bms/voltage_now"
#define CURR_NOW "/sys/class/power_supply/bms/current_now"

/* 25C OCV-to-SoC Curve, unit: 0.1mV (100% down to 0%, 56-point profile for J17 Cosmx 4820mAh) */
static const uint32_t LUT[56] = {
    44364, 44133, 43920, 43709, 43490, 43266, 43033, 42791, 42544, 42298,
    42051, 41811, 41575, 41344, 41121, 40905, 40696, 40493, 40300, 40126,
    39950, 39727, 39461, 39239, 39075, 38934, 38808, 38695, 38594, 38503,
    38422, 38349, 38284, 38226, 38177, 38130, 38086, 38043, 37983, 37893,
    37785, 37664, 37522, 37353, 37191, 37094, 37066, 37049, 37031, 36980,
    36703, 36246, 35647, 34828, 33540, 30000
};

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void log_msg(const char *fmt, ...) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f, "[%s] ", time_buf);

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");
    fclose(f);
}

static int read_int_file(const char *path, long *val) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (fscanf(f, "%ld", val) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int write_str_file(const char *path, const char *str) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(str, f);
    fclose(f);
    return 0;
}

static int soc_of(int index) {
    if (index <= 45) {
        return 100 - (2 * index);
    } else {
        return 55 - index;
    }
}

static int soc_from_mv(uint32_t mv) {
    if (mv >= LUT[0]) return 100;
    if (mv <= LUT[55]) return 0;

    for (int i = 0; i < 55; i++) {
        uint32_t a = LUT[i];
        uint32_t b = LUT[i + 1];
        if (mv <= a && mv >= b) {
            int sa = soc_of(i);
            int sb = soc_of(i + 1);
            if (a == b) return sa;
            int res = sa - (int)((long)(a - mv) * (sa - sb) / (a - b));
            if (res > 100) res = 100;
            if (res < 0) res = 0;
            return res;
        }
    }
    return 0;
}

static int do_calibrate(void) {
    /* 1. Trigger FG Reset */
    write_str_file(FG_RESET, "1\n");
    sleep(3);

    /* 2. Read kdata buffer (412 bytes) from /dev/qg */
    int fd = open(QG_DEV, O_RDONLY);
    if (fd < 0) {
        log_msg("ERROR: Cannot open %s", QG_DEV);
        return -1;
    }

    uint8_t kdata[412];
    ssize_t n = read(fd, kdata, sizeof(kdata));
    close(fd);

    if (n != sizeof(kdata)) {
        log_msg("ERROR: Incomplete kdata read (%zd bytes)", n);
        return -1;
    }

    /* Extract 32-bit OCV (microvolts) from offset 300 */
    uint32_t ocv = *(uint32_t *)(kdata + 300);
    if (ocv < 3000000 || ocv > 5000000) {
        log_msg("ERROR: Invalid OCV reading: %u uV", ocv);
        return -1;
    }

    int soc = soc_from_mv(ocv / 100);

    long cap = 0;
    read_int_file(CAPACITY, &cap);

    log_msg("OCV=%u uV -> Calculated: %%%d, Displayed: %%%ld", ocv, soc, cap);

    long diff = labs(soc - cap);
    if (diff < 3) {
        log_msg("Divergence <%ld, chip state is already aligned", diff);
        return 0;
    }

    /* 3. Prepare 144-byte udata calibration payload */
    uint32_t udata[36];
    memset(udata, 0, sizeof(udata));
    udata[0] = (uint32_t)soc;
    udata[1] = 1;   /* soc valid */
    udata[2] = ocv;
    udata[3] = 1;   /* ocv valid */

    write_str_file(FG_RESET, "1\n");
    sleep(2);

    /* 4. Write udata back to /dev/qg */
    fd = open(QG_DEV, O_WRONLY);
    if (fd < 0) {
        log_msg("ERROR: Cannot open %s for writing", QG_DEV);
        return -1;
    }

    n = write(fd, udata, sizeof(udata));
    close(fd);

    if (n != sizeof(udata)) {
        log_msg("ERROR: Failed to write udata payload (%zd bytes)", n);
        return -1;
    }

    sleep(3);
    long new_cap = 0;
    read_int_file(CAPACITY, &new_cap);
    log_msg("Calibration successful! Capacity updated to: %%%ld", new_cap);

    return 0;
}

static void light_check(void) {
    long cur = 0;
    if (read_int_file(CURR_NOW, &cur) != 0) return;

    /* Skip if phone is actively drawing heavy current (>100mA) */
    if (labs(cur) > 100000) return;

    long v = 0;
    if (read_int_file(VOLT_NOW, &v) != 0) return;

    int est_soc = soc_from_mv((uint32_t)(v / 100));

    long cap = 0;
    if (read_int_file(CAPACITY, &cap) != 0) return;

    if (labs(est_soc - cap) >= 3) {
        log_msg("Divergence detected: V=%ld mV, Est=%%%d, Disp=%%%ld -> Calibrating",
                v / 1000, est_soc, cap);
        do_calibrate();
    }
}

static void check_log_rotate(void) {
    struct stat st;
    if (stat(LOG_FILE, &st) == 0) {
        if (st.st_size > 200 * 1024) { /* 200 KB threshold */
            unlink(LOG_FILE);
            log_msg("Log rotated.");
        }
    }
}

static void wait_for_boot(void) {
    int tries = 0;
    while (tries < 120 && g_running) {
        char buf[16] = {0};
        FILE *f = popen("getprop sys.boot_completed", "r");
        if (f) {
            if (fgets(buf, sizeof(buf), f)) {
                if (buf[0] == '1') {
                    pclose(f);
                    break;
                }
            }
            pclose(f);
        }
        sleep(5);
        tries++;
    }
    sleep(15);
}

int main(int argc, char **argv) {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        log_msg("One-shot manual test executed.");
        return do_calibrate();
    }

    log_msg("qg-daemon v4.0 started (Native C).");

    wait_for_boot();
    do_calibrate();

    int cycle = 0;
    while (g_running) {
        sleep(60);
        cycle++;

        /* Periodic 2-hour guarantee sync */
        if (cycle >= 120) {
            log_msg("Periodic 2-hour synchronization started.");
            do_calibrate();
            cycle = 0;
        } else {
            light_check();
        }

        if (cycle % 60 == 0) {
            check_log_rotate();
        }
    }

    log_msg("qg-daemon terminated.");
    return 0;
}
