/*
 * qg_daemon.c - Xiaomi Redmi Note 9 Pro / Mi 10T Lite (gauguin) Open-Source Battery Daemon
 * Fixes Qualcomm Fuel Gauge (QG) desynchronization and freezing percentage on Custom ROMs.
 * Root Cause Fix: Dynamic IR Compensation, Active Healthd Sync & Smooth Step Decay
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
#define QG_DEV    "/dev/qg"
#define FG_RESET "/sys/class/power_supply/bms/fg_reset"
#define CAPACITY "/sys/class/power_supply/battery/capacity"
#define VOLT_NOW "/sys/class/power_supply/battery/voltage_now"
#define CURR_NOW "/sys/class/power_supply/battery/current_now"

#define R_INT_MOHM 100

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

static int check_is_charging(void) {
    FILE *f = fopen("/sys/class/power_supply/battery/status", "r");
    if (!f) return 0;
    char status[32] = {0};
    if (fscanf(f, "%31s", status) == 1) {
        fclose(f);
        if (strcmp(status, "Charging") == 0 || strcmp(status, "Full") == 0) {
            return 1;
        }
    } else {
        fclose(f);
    }
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

static int apply_soc(int target_soc, uint32_t ocv) {
    uint32_t udata[36];
    memset(udata, 0, sizeof(udata));
    udata[0] = (uint32_t)target_soc;
    udata[1] = 1;
    udata[2] = ocv;
    udata[3] = 1;

    write_str_file(FG_RESET, "1\n");
    usleep(200000);

    int fd = open(QG_DEV, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t n = write(fd, udata, sizeof(udata));
    close(fd);

    system("cmd battery reset 2>/dev/null");

    return (n == sizeof(udata)) ? 0 : -1;
}

static int do_calibrate(int forced_target) {
    write_str_file(FG_RESET, "1\n");
    usleep(300000);

    int fd = open(QG_DEV, O_RDONLY);
    if (fd < 0) return -1;

    uint8_t kdata[412];
    ssize_t n = read(fd, kdata, sizeof(kdata));
    close(fd);

    if (n != sizeof(kdata)) return -1;

    uint32_t ocv = *(uint32_t *)(kdata + 300);
    if (ocv < 3000000 || ocv > 5000000) return -1;

    int target = (forced_target >= 0) ? forced_target : soc_from_mv(ocv / 100);

    if (apply_soc(target, ocv) == 0) {
        log_msg("Gosterge Guncellendi: %%%d", target);
        return 0;
    }
    return -1;
}

static void smart_check(void) {
    long v_uv = 0, cur_ua = 0, cap = 0;
    if (read_int_file(VOLT_NOW, &v_uv) != 0) return;
    read_int_file(CURR_NOW, &cur_ua);
    if (read_int_file(CAPACITY, &cap) != 0) return;

    long v_mv = v_uv / 1000;
    long cur_ma = cur_ua / 1000;

    long v_comp_uv = v_uv + (cur_ma * R_INT_MOHM);
    if (v_comp_uv < 3000000) v_comp_uv = 3000000;

    int est_soc = soc_from_mv((uint32_t)(v_comp_uv / 100));
    int charging = check_is_charging();

    if (!charging) {
        if (est_soc < cap) {
            do_calibrate((int)cap - 1);
        }
    } else {
        if (est_soc > cap + 1) {
            do_calibrate((int)cap + 1);
        }
    }
}

int main(int argc, char **argv) {
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    if (argc > 1 && strcmp(argv[1], "test") == 0) {
        return do_calibrate(-1);
    }

    log_msg("qg-daemon v4.4 baslatildi (Smooth Step Decay Enabled).");
    do_calibrate(-1);

    while (g_running) {
        sleep(60);
        smart_check();
    }

    log_msg("qg-daemon durduruldu.");
    return 0;
}
