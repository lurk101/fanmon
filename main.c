#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include "fan.h"
#include "temp.h"

static float high_temp_threshold = 75.0; // Target max. die temp
static float low_temp_threshold = 60.0;  // Fan start temp.
static volatile uint8_t running = 1u;    // Abort (CTL-C) flag
static int log_level = 0;                // Log verbosity
static int min_fan = 0;

// Catch terminal events
static void ctlc_handler(int s) { running = 0; }

// display help to stdout
static void help(char* av) {
    printf("\nUsage:\n\n"
           "%s [-v loglevel] [-l lowTemp] [-h highTemp] \n\n"
           "  -v Set log verbosity. 0 - quiet (default), 1-chatty, 2-debug\n"
           "  -l Low temperature threshold in Celcius (default - %d)\n"
           "  -m High temperature threshold in Celcius (default - %d)\n"
           "  -p minimum fan speed as percentage (default - %d)\n\n",
           av, (int)low_temp_threshold, (int)high_temp_threshold, min_fan);
}

// Use exponential smoothing
static uint8_t Average(float p) {
    static float sum = 0;
    sum = (sum + sum + p) / 3;
    return sum;
}

// Run once per sampling interval process
static int process_interval(void) {
    uint32_t temp = getTemp();
    // Get the temperature and set the fan speed
    if (temp == 0) {
        fprintf(stderr, SD_ALERT "%sError retrieving temperature\n");
        return -1;
    }
    // Calculate new fan speed in the 0-127 range
    float p;
    if (temp <= low_temp_threshold)
        p = min_fan;
    else if (temp >= high_temp_threshold)
        p = 127;
    else
        p = min_fan + ((temp - low_temp_threshold) * (127.0 - min_fan)) /
                          (high_temp_threshold - low_temp_threshold);
    // Apply smoothing
    uint32_t ap = Average(p);
    // Set the fan speed
    fanPower(ap);
    // Conditionally log the full status
    static uint32_t lastP = 0;
    // for verbosity >= 1, log fan on/off transitions
    if (log_level > 0) {
        if ((lastP && !ap) || (!lastP && ap))
            fprintf(stderr, SD_INFO "Fan o%s\n", ap ? "n" : "ff");
        lastP = ap;
    }
    // for verbosity > 1, log die temp and fan rpm at every interval
    if (log_level > 1)
        fprintf(stderr, SD_INFO "FAN %u% TEMP %u\n", (ap * 100) / 128, temp);
    return 0;
}

int main(int ac, char* av[]) {
    const uint32_t poll_interval = 2;

    // Parse parameters
    int opt;
    while ((opt = getopt(ac, av, "v:l:m:p:h")) != -1) {
        switch (opt) {
        case 'v':
            log_level = atoi(optarg);
            break;
        case 'l':
            low_temp_threshold = atoi(optarg);
            break;
        case 'm':
            high_temp_threshold = atoi(optarg);
            break;
        case 'p':
            min_fan = (atoi(optarg) * 127) / 100;
            break;
        case 'h':
            help(av[0]);
            exit(0);
        case ':':
            fprintf(stderr, SD_ALERT "option needs a value\n");
            help(av[0]);
            goto error;
        case '?':
            fprintf(stderr, SD_ALERT "unknown option: %c\n", optopt);
            help(av[0]);
            goto error;
        }
    }

    // Starting
    fprintf(stderr, SD_INFO "Fan control starting, log level %d\n", log_level);

    // Intercept sigterm
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = ctlc_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGHUP, &sigIntHandler, NULL);
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

    // Inform systemd we've started
    sd_notify(0, "READY=1");

    // Poll forever
    for (; running;) {
        if (process_interval())
            goto error;
        sleep(poll_interval);
    }

    // Time to exit...
    fanClose();
    tempClose();
    fprintf(stderr, SD_INFO "Stopped\n");
    return 0;

error:
    // Exit with error.
    fanClose();
    tempClose();
    fprintf(stderr, SD_ALERT "Stopped with error\n");
    sd_notifyf(0,
               "STATUS=Failed: %s\n"
               "ERRNO=%d",
               strerror(errno), errno);
    return -1;
}
