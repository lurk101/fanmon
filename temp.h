#pragma once
#include <stdint.h>

// Set up temperature sensing
int tempOpen(int thermal_zone);

// Tear down temperature sensing
void tempClose(void);

// Retrieve die temperature
uint32_t getTemp(void);
