#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include "fan.h"
#include "temp.h"

static volatile uint8_t running = 1;     // Abort (CTL-C) flag
static int log_level = 0;                // Log verbosity

static struct {
    uint32_t level, temp;
} curve[] = {{72, 45},  {94, 50},  {117, 55}, {139, 60},  {162, 65},
             {184, 70}, {207, 75}, {229, 80}, {255, 1000}};

// Catch terminal events
static void ctlc_handler(int s) { running = 0; }

// display help to stdout
static void help(char* av) {
    printf("\nUsage:\n\n"
           "%s [-v loglevel]\n\n"
           "  -v Set log verbosity. 0 - quiet (default), 1-chatty, 2-debug\n",
           av);
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
    int32_t p;
    for (int i = 0; i < sizeof(curve) / sizeof(curve[0]); i++)
        if (temp < curve[i].temp) {
            p = curve[i].level;
            break;
        }
    // Set the fan speed
    fanPower(p);
    // Conditionally log the full status
    static uint32_t lastP = 0;
    // for verbosity >= 1, log fan on/off transitions
    if (log_level > 0) {
        if ((lastP && !p) || (!lastP && p))
            fprintf(stderr, SD_INFO "Fan o%s\n", p ? "n" : "ff");
        lastP = p;
    }
    // for verbosity > 1, log die temp and fan rpm at every interval
    if (log_level > 1)
        fprintf(stderr, SD_INFO "FAN %u% TEMP %u\n", (uint32_t)(p * 100 / 256),
                temp);
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
    return -1;
}
