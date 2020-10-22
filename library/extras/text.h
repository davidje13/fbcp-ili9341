#pragma once

#include <inttypes.h>

#include "../Framebuffer.h"

void DrawText(const Framebuffer &framebuffer, const char *text, int x, int y, uint16_t color, uint16_t bgColor);
