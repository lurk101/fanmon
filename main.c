#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include "fan.h"
#include "temp.h"

static volatile uint8_t running = 1, reloading = 0; // Abort (CTL-C) flag

// Catch terminal events
static void ctlc_handler(int s) { running = 0; }

static void hup_handler(int s) { reloading = 1; }

static volatile int32_t p, temp;

// Run once per sampling interval process
static int process_interval(void) {
    if (reloading) {
        sd_notify(0, "RELOADING=1");
        sleep(1);
        fprintf(stderr, SD_INFO "Temp %d, Fan at %d\%\n", temp, p * 100 / 256);
        reloading = 0;
    }
    temp = getTemp();
    // Get the temperature and set the fan speed
    if (temp == 0) {
        fprintf(stderr, SD_ALERT "%sError retrieving temperature\n");
        return -1;
    }
    // Set the fan speed
    p = (((float)temp - 50.0) / 20.0) * 256;
    if (p > 255)
        p = 255;
    else if (p < 0)
        p = 0;
    fanPower(p);
    return 0;
}

int main(int ac, char* av[]) {
    const uint32_t poll_interval = 3;

    fprintf(stderr, SD_INFO "Fan control starting\n");

    // Intercept sigterm
    struct sigaction sigIntHandler, sigIntHandler2;
    sigIntHandler.sa_handler = ctlc_handler;
    sigIntHandler2.sa_handler = hup_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigemptyset(&sigIntHandler2.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigIntHandler2.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGHUP, &sigIntHandler2, NULL);
    sigaction(SIGSTOP, &sigIntHandler, NULL);
    sigaction(SIGTERM, &sigIntHandler, NULL);
    sigaction(SIGKILL, &sigIntHandler, NULL);

    // Initialize the library
    if (fanInit() < 0) {
        fprintf(stderr, SD_ERR "Must run as ROOT\n");
        errno = EPERM;
        goto error;
    }

    if (tempOpen() < 0) {
        fprintf(stderr, SD_ALERT "Can't init temperature sensor\n");
        errno = EPERM;
        goto error;
    }
    sd_notify(0, "READY=1");
    // Poll forever
    for (; running;) {
        if (process_interval())
            goto error;
        sleep(poll_interval);
    }
    sd_notify(0, "STOPPING=1");

    // Time to exit...
    fanClose();
    tempClose();
    fprintf(stderr, SD_INFO "Stopped\n");
    return 0;

error:
    // Exit with error.
    sd_notify(0, "STOPPING=1");
    fanClose();
    tempClose();
    fprintf(stderr, SD_ALERT "Stopped with error\n");
    return -1;
}
