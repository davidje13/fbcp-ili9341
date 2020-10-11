#pragma once

#include <inttypes.h>

#include "Framebuffer.h"

// Spans track dirty rectangular areas on screen
struct Span
{
  uint16_t x, endX, y, endY, lastScanEndX;
  uint32_t size; // Specifies a box of width [x, endX[ * [y, endY[, where scanline endY-1 can be partial, and ends in lastScanEndX.
  Span *next; // Maintain a linked skip list inside the array for fast seek to next active element when pruning
};

void InitDiff(int maxWidth, int maxHeight);

Span* ComputeDiff(
  const Framebuffer &framebuffer,
  const Framebuffer &prevFramebuffer,
  bool changed,
  bool interlacedDiff,
  int interlacedFieldParity
);
