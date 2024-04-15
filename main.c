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
static volatile int32_t p, temp;
static int throttle_temp = 80;
static int start_temp = 50;

// Catch terminal events
static void ctlc_handler(int s) { running = 0; }

static void hup_handler(int s) { reloading = 1; }

// Run once per sampling interval process
static int process_interval(void) {
    if (reloading) {
        sd_notify(0, "RELOADING=1");
        sleep(1);
        fprintf(stderr, SD_INFO "Temp %d, Fan at %d\%\n", getTemp(),
                p * 100 / 256);
        reloading = 0;
    }
    temp = getTemp();
    // Get the temperature and set the fan speed
    if (temp == 0) {
        fprintf(stderr, SD_ERR "%sError retrieving temperature\n");
        return -1;
    }
    // Set the fan speed
    p = (((float)temp - start_temp) / ((throttle_temp - 10) - start_temp)) *
        256;
    if (p < 0)
        p = 0;
    else if (p > 255)
        p = 255;
    fanPower(p);
    return 0;
}

static void help(void) {
    fprintf(stderr, SD_ERR "Usage: fanmon [-t thermal_zone] [-p pwm_chip] [-m "
                           "throttle_temp] [-s start_temp]\n");
    exit(-1);
}

int main(int ac, char* av[]) {
    const uint32_t poll_interval = 3;
    int thermal_zone = 0, pwm_chip = 0;
    int c;

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

    opterr = 0;
    while ((c = getopt(ac, av, "t:p:m:s:")) != -1)
        switch (c) {
        case 't':
            thermal_zone = atoi(optarg);
            break;
        case 'p':
            pwm_chip = atoi(optarg);
            break;
        case 'm':
            throttle_temp = atoi(optarg);
            break;
        case 's':
            start_temp = atoi(optarg);
            break;
        case '?':
            fprintf(stderr, SD_ERR "Unknown option `-%c'.\n", optopt);
        default:
            help();
        }
    fprintf(stderr, SD_INFO "Using thermal zone %d, pwm chip %d\n",
            thermal_zone, pwm_chip);
    fprintf(stderr, SD_INFO "Using start temp %dC, throttle temp %dC\n",
            start_temp, throttle_temp);

    // Initialize the library
    if (fanInit(pwm_chip) < 0) {
        fprintf(stderr, SD_ERR "Must run as ROOT\n");
        errno = EPERM;
        goto error;
    }

    if (tempOpen(thermal_zone) < 0) {
        fprintf(stderr, SD_ERR "Can't init temperature sensor\n");
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
    fprintf(stderr, SD_ERR "Stopped with error\n");
    return -1;
}
