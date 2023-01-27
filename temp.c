#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include "fan.h"

#define TEMP_FN "/sys/class/thermal/thermal_zone0/temp"

// Interval count, only used with debug log level
static int fh = -1;

// Initialize the fan controller
int tempOpen(void) {
    fh = open(TEMP_FN, O_RDONLY);
    if (fh < 0) {
        fprintf(stderr, SD_ALERT "Failed to open %s\n", TEMP_FN);
        return -1;
    }
    return 0;
}

// Release the fan controller
void tempClose(void) {
    if (fh >= 0)
        close(fh);
}

// Set the fan power level
uint32_t getTemp(void) {
    lseek(fh, 0, SEEK_SET);
    char buf[16];
    ssize_t l = read(fh, buf, sizeof(buf) - 1);
    if (l < 0) {
        fprintf(stderr, SD_ALERT "Failed to read %s\n", TEMP_FN);
        return 0;
    }
    buf[l] = 0;
    return atoi(buf) / 1000;
}
