#pragma once
#include <stdint.h>

// Initialize the fan controller
int fanInit(void);

// Release the fan controller
void fanClose(void);

// Set the fan power level
void fanPower(uint32_t s);
