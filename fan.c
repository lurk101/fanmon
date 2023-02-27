#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include "fan.h"

#define FAN_FN "/sys/class/pwm/pwmchip8/"

#define PWM_PERIOD 42000

// Interval count, only used with debug log level
static int fh = -1;

// Initialize the fan controller
static int fanWrite(const char* fn, const char* data) {
    usleep(100000);
    fh = open(fn, O_WRONLY);
    if (fh < 0) {
        fprintf(stderr, SD_ALERT "Failed to open %s\n", fn);
        return -1;
    }
    if (write(fh, data, sizeof(data)) < 0) {
        fprintf(stderr, SD_ALERT "Failed to write %s\n", fn);
        return -1;
    }
    close(fh);
    return 0;
}

int fanInit(void) {
    if (fanWrite(FAN_FN "export", "0"))
        return -1;
    char buf[16];
    sprintf(buf, "%d", PWM_PERIOD);
    if (fanWrite(FAN_FN "pwm0/period", buf))
        return -1;
    if (fanWrite(FAN_FN "pwm0/duty_cycle", "0"))
        return -1;
    if (fanWrite(FAN_FN "pwm0/polarity", "normal"))
        return -1;
    if (fanWrite(FAN_FN "pwm0/enable", "1"))
        return -1;
    return 0;
}

// Release the fan controller
void fanClose(void) {
    if (fh >= 0)
        close(fh);
    fanWrite(FAN_FN "unexport", "0");
}

// Set the fan power level
void fanPower(uint32_t s) {
    if (s > 127)
        s = 127;
    char buf[16];
    sprintf(buf, "%d", (s * PWM_PERIOD) / 128);
    fanWrite(FAN_FN "pwm0/duty_cycle", buf);
}
