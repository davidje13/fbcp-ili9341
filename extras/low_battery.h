#pragma once

#include <inttypes.h>

// This functions must be called during the startup of the program to initialize
// internal data related to rendering the low battery icon.
void InitLowBatterySystem();

// Draws a low battery icon on the given framebuffer.
void DrawLowBatteryIcon(uint16_t *framebuffer, int scanlineStrideBytes);
