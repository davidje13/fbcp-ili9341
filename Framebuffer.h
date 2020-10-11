#pragma once

#include <inttypes.h>

typedef struct {
  uint16_t *data;
  int width;
  int height;
  int strideBytes;
} Framebuffer;

#define FRAMEBUFFER_PIXEL(buf, x, y) (*(uint16_t*) (((uint8_t*) buf.data) + ((y)*buf.strideBytes+((x)<<1))))
