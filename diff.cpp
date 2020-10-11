#include "config.h"
#include "diff.h"
#include "util.h"
#include "display.h"
#include "mem_alloc.h"

// Looking at SPI communication in a logic analyzer, it is observed that waiting for the finish of an SPI command FIFO causes pretty exactly one byte of delay to the command stream.
// Therefore the time/bandwidth cost of ending the current span and starting a new span is as follows:
// 1 byte to wait for the current SPI FIFO batch to finish,
// +1 byte to send the cursor X coordinate change command,
// +1 byte to wait for that FIFO to flush,
// +2 bytes to send the new X coordinate,
// +1 byte to wait for the FIFO to flush again,
// +1 byte to send the data_write command,
// +1 byte to wait for that FIFO to flush,
// after which the communication is ready to start pushing pixels. This totals to 8 bytes, or 4 pixels, meaning that if there are 4 unchanged pixels or less between two adjacent dirty
// spans, it is all the same to just update through those pixels as well to not have to wait to flush the FIFO.
#if defined(ALL_TASKS_SHOULD_DMA)
#define SPAN_MERGE_THRESHOLD 320
#elif defined(DISPLAY_SPI_BUS_IS_16BITS_WIDE)
#define SPAN_MERGE_THRESHOLD 10
#elif defined(HX8357D)
#define SPAN_MERGE_THRESHOLD 6
#else
#define SPAN_MERGE_THRESHOLD 4
#endif

static Span *spans = (Span*) 0;
static int frameWidth = 0;
static int frameHeight = 0;
static int frameScanlineStrideBytes = 0;

void InitDiff(int maxWidth, int maxHeight) {
  spans = (Span*)Malloc((maxWidth * maxHeight / 2) * sizeof(Span), "main() task spans");
}

// Naive non-diffing functionality: just submit the whole display contents
static Span* NoDiffChangedRectangle()
{
  spans->x = 0;
  spans->endX = spans->lastScanEndX = frameWidth;
  spans->y = 0;
  spans->endY = frameHeight;
  spans->size = frameWidth*frameHeight;
  spans->next = 0;
  return spans;
}

#ifdef UPDATE_FRAMES_IN_SINGLE_RECTANGULAR_DIFF
// Coarse diffing of two framebuffers with tight stride, 16 pixels at a time
// Finds the first changed pixel, coarse result aligned down to 8 pixels boundary
static int coarse_linear_diff(uint16_t *framebuffer, uint16_t *prevFramebuffer, uint16_t *framebufferEnd)
{
  uint16_t *endPtr;
  asm volatile(
    "mov r0, %[framebufferEnd]\n" // r0 <- pointer to end of current framebuffer
    "mov r1, %[framebuffer]\n"   // r1 <- current framebuffer
    "mov r2, %[prevFramebuffer]\n" // r2 <- framebuffer of previous frame

  "start_%=:\n"
    "pld [r1, #128]\n" // preload data caches for both current and previous framebuffers 128 bytes ahead of time
    "pld [r2, #128]\n"

    "ldmia r1!, {r3,r4,r5,r6}\n" // load 4x32-bit elements (8 pixels) of current framebuffer
    "ldmia r2!, {r7,r8,r9,r10}\n" // load corresponding 4x32-bit elements (8 pixels) of previous framebuffer
    "cmp r3, r7\n" // compare all 8 pixels if they are different
    "cmpeq r4, r8\n"
    "cmpeq r5, r9\n"
    "cmpeq r6, r10\n"
    "bne end_%=\n" // if we found a difference, we are done

    // Unroll once for another set of 4x32-bit elements. On Raspberry Pi Zero, data cache line is 32 bytes in size, so one iteration
    // of the loop computes a single data cache line, with preloads in place at the top.
    "ldmia r1!, {r3,r4,r5,r6}\n"
    "ldmia r2!, {r7,r8,r9,r10}\n"
    "cmp r3, r7\n"
    "cmpeq r4, r8\n"
    "cmpeq r5, r9\n"
    "cmpeq r6, r10\n"
    "bne end_%=\n" // if we found a difference, we are done

    "cmp r0, r1\n" // framebuffer == framebufferEnd? did we finish through the array?
    "bne start_%=\n"
    "b done_%=\n"

  "end_%=:\n"
    "sub r1, r1, #16\n" // ldmia r1! increments r1 after load, so subtract back the last increment in order to not shoot past the first changed pixels

  "done_%=:\n"
    "mov %[endPtr], r1\n" // output endPtr back to C code
    : [endPtr]"=r"(endPtr)
    : [framebuffer]"r"(framebuffer), [prevFramebuffer]"r"(prevFramebuffer), [framebufferEnd]"r"(framebufferEnd)
    : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "cc"
  );
  return endPtr - framebuffer;
}

// Same as coarse_linear_diff, but finds the last changed pixel in linear order instead of first, i.e.
// Finds the last changed pixel, coarse result aligned up to 8 pixels boundary
static int coarse_backwards_linear_diff(uint16_t *framebuffer, uint16_t *prevFramebuffer, uint16_t *framebufferEnd)
{
  uint16_t *endPtr;
  asm volatile(
    "mov r0, %[framebufferBegin]\n" // r0 <- pointer to beginning of current framebuffer
    "mov r1, %[framebuffer]\n"   // r1 <- current framebuffer (starting from end of framebuffer)
    "mov r2, %[prevFramebuffer]\n" // r2 <- framebuffer of previous frame (starting from end of framebuffer)

  "start_%=:\n"
    "pld [r1, #-128]\n" // preload data caches for both current and previous framebuffers 128 bytes ahead of time
    "pld [r2, #-128]\n"

    "ldmdb r1!, {r3,r4,r5,r6}\n" // load 4x32-bit elements (8 pixels) of current framebuffer
    "ldmdb r2!, {r7,r8,r9,r10}\n" // load corresponding 4x32-bit elements (8 pixels) of previous framebuffer
    "cmp r3, r7\n" // compare all 8 pixels if they are different
    "cmpeq r4, r8\n"
    "cmpeq r5, r9\n"
    "cmpeq r6, r10\n"
    "bne end_%=\n" // if we found a difference, we are done

    // Unroll once for another set of 4x32-bit elements. On Raspberry Pi Zero, data cache line is 32 bytes in size, so one iteration
    // of the loop computes a single data cache line, with preloads in place at the top.
    "ldmdb r1!, {r3,r4,r5,r6}\n"
    "ldmdb r2!, {r7,r8,r9,r10}\n"
    "cmp r3, r7\n"
    "cmpeq r4, r8\n"
    "cmpeq r5, r9\n"
    "cmpeq r6, r10\n"
    "bne end_%=\n" // if we found a difference, we are done

    "cmp r0, r1\n" // framebuffer == framebufferEnd? did we finish through the array?
    "bne start_%=\n"
    "b done_%=\n"

  "end_%=:\n"
    "add r1, r1, #16\n" // ldmdb r1! decrements r1 before load, so add back the last decrement in order to not shoot past the first changed pixels

  "done_%=:\n"
    "mov %[endPtr], r1\n" // output endPtr back to C code
    : [endPtr]"=r"(endPtr)
    : [framebuffer]"r"(framebufferEnd), [prevFramebuffer]"r"(prevFramebuffer+(framebufferEnd-framebuffer)), [framebufferBegin]"r"(framebuffer)
    : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "cc"
  );
  return endPtr - framebuffer;
}

static Span* DiffFramebuffersToSingleChangedRectangle(uint16_t *framebuffer, uint16_t *prevFramebuffer)
{
  int minY = 0;
  int minX = -1;

  const int stride = frameScanlineStrideBytes>>1; // Stride as uint16 elements.
  const int WidthAligned4 = (uint32_t)frameWidth & ~3u;

  uint16_t *scanline = framebuffer;
  uint16_t *prevScanline = prevFramebuffer;

  static const bool framebufferSizeCompatibleWithCoarseDiff = frameScanlineStrideBytes == frameWidth*2 && frameScanlineStrideBytes*frameHeight % 32 == 0;
  if (framebufferSizeCompatibleWithCoarseDiff)
  {
    int numPixels = frameWidth*frameHeight;
    int firstDiff = coarse_linear_diff(framebuffer, prevFramebuffer, framebuffer + numPixels);
    if (firstDiff == numPixels)
      return 0; // No pixels changed, nothing to do.
    // Coarse diff computes a diff at 8 adjacent pixels at a time, and returns the point to the 8-pixel aligned coordinate where the pixels began to differ.
    // Compute the precise diff position here.
    while(framebuffer[firstDiff] == prevFramebuffer[firstDiff]) ++firstDiff;
    minX = firstDiff % frameWidth;
    minY = firstDiff / frameWidth;
  }
  else
  {
    while(minY < frameHeight)
    {
      int x = 0;
      // diff 4 pixels at a time
      for(; x < WidthAligned4; x += 4)
      {
        uint64_t diff = *(uint64_t*)(scanline+x) ^ *(uint64_t*)(prevScanline+x);
        if (diff)
        {
          minX = x + (__builtin_ctzll(diff) >> 4);
          goto found_top;
        }
      }
      // tail unaligned 0-3 pixels one by one
      for(; x < frameWidth; ++x)
      {
        uint16_t diff = *(scanline+x) ^ *(prevScanline+x);
        if (diff)
        {
          minX = x;
          goto found_top;
        }
      }
      scanline += stride;
      prevScanline += stride;
      ++minY;
    }
    return 0; // No pixels changed, nothing to do.
  }
found_top:

  int maxX = -1;
  int maxY = frameHeight-1;

  if (framebufferSizeCompatibleWithCoarseDiff)
  {
    int numPixels = frameWidth*frameHeight;
    int firstDiff = coarse_backwards_linear_diff(framebuffer, prevFramebuffer, framebuffer + numPixels);
    // Coarse diff computes a diff at 8 adjacent pixels at a time, and returns the point to the 8-pixel aligned coordinate where the pixels began to differ.
    // Compute the precise diff position here.
    while(firstDiff > 0 && framebuffer[firstDiff] == prevFramebuffer[firstDiff]) --firstDiff;
    maxX = firstDiff % frameWidth;
    maxY = firstDiff / frameWidth;
  }
  else
  {
    scanline = framebuffer + (frameHeight - 1)*stride;
    prevScanline = prevFramebuffer + (frameHeight - 1)*stride; // (same scanline from previous frame, not preceding scanline)

    while(maxY >= minY)
    {
      int x = frameWidth-1;
      // tail unaligned 0-3 pixels one by one
      for(; x >= WidthAligned4; --x)
      {
        if (scanline[x] != prevScanline[x])
        {
          maxX = x;
          goto found_bottom;
        }
      }
      // diff 4 pixels at a time
      x = x & ~3u;
      for(; x >= 0; x -= 4)
      {
        uint64_t diff = *(uint64_t*)(scanline+x) ^ *(uint64_t*)(prevScanline+x);
        if (diff)
        {
          maxX = x + 3 - (__builtin_clzll(diff) >> 4);
          goto found_bottom;
        }
      }
      scanline -= stride;
      prevScanline -= stride;
      --maxY;
    }
  }
found_bottom:

  scanline = framebuffer + minY*stride;
  prevScanline = prevFramebuffer + minY*stride;
  int lastScanEndX = maxX;
  if (minX > maxX) SWAPU32(minX, maxX);
  int leftX = 0;
  while(leftX < minX)
  {
    uint16_t *s = scanline + leftX;
    uint16_t *prevS = prevScanline + leftX;
    for(int y = minY; y <= maxY; ++y)
    {
      if (*s != *prevS)
        goto found_left;
      s += stride;
      prevS += stride;
    }
    ++leftX;
  }
found_left:

  int rightX = frameWidth-1;
  while(rightX > maxX)
  {
    uint16_t *s = scanline + rightX;
    uint16_t *prevS = prevScanline + rightX;
    for(int y = minY; y <= maxY; ++y)
    {
      if (*s != *prevS)
        goto found_right;
      s += stride;
      prevS += stride;
    }
    --rightX;
  }
found_right:

  spans->x = leftX;
  spans->endX = rightX+1;
  spans->lastScanEndX = lastScanEndX+1;
  spans->y = minY;
  spans->endY = maxY+1;

#if defined(ALIGN_DIFF_TASKS_FOR_32B_CACHE_LINES) && defined(ALL_TASKS_SHOULD_DMA)
  // Make sure the task is a multiple of 32 bytes wide so we can use a fast DMA copy
  // algorithm later on. Currently this is only exploited in dma.cpp if ALL_TASKS_SHOULD_DMA
  // option is enabled, so only enable it there.
  spans->x = MAX(0, ALIGN_DOWN(spans->x, 16));
  spans->endX = MIN(frameWidth, ALIGN_UP(spans->endX, 16));
  spans->lastScanEndX = ALIGN_UP(spans->lastScanEndX, 16);
#endif

  spans->size = (
    (spans->endX - spans->x) *
    (spans->endY - spans->y - 1) +
    (spans->lastScanEndX - spans->x)
  );
  spans->next = 0;
  return spans;
}
#endif

static Span* DiffFramebuffersToScanlineSpansFastAndCoarse4Wide(uint16_t *framebuffer, uint16_t *prevFramebuffer, bool interlacedDiff, int interlacedFieldParity)
{
  int numSpans = 0;
  int y = interlacedDiff ? interlacedFieldParity : 0;
  int yInc = interlacedDiff ? 2 : 1;
  // If doing an interlaced update, skip over every second scanline.
  int scanlineInc = interlacedDiff ? (frameScanlineStrideBytes>>2) : (frameScanlineStrideBytes>>3);
  uint64_t *scanline = (uint64_t *)(framebuffer + y*(frameScanlineStrideBytes>>1));
  uint64_t *prevScanline = (uint64_t *)(prevFramebuffer + y*(frameScanlineStrideBytes>>1)); // (same scanline from previous frame, not preceding scanline)

  const int W = frameWidth>>2;

  Span *span = spans;
  while(y < frameHeight)
  {
    uint16_t *scanlineStart = (uint16_t *)scanline;

    for(int x = 0; x < W;)
    {
      if (scanline[x] != prevScanline[x])
      {
        uint16_t *spanStart = (uint16_t *)(scanline + x) + (__builtin_ctzll(scanline[x] ^ prevScanline[x]) >> 4);
        ++x;

        // We've found a start of a span of different pixels on this scanline, now find where this span ends
        uint16_t *spanEnd;
        for(;;)
        {
          if (x < W)
          {
            if (scanline[x] != prevScanline[x])
            {
              ++x;
              continue;
            }
            else
            {
              spanEnd = (uint16_t *)(scanline + x) + 1 - (__builtin_clzll(scanline[x-1] ^ prevScanline[x-1]) >> 4);
              ++x;
              break;
            }
          }
          else
          {
            spanEnd = scanlineStart + frameWidth;
            break;
          }
        }

        // Submit the span update task
        span->x = spanStart - scanlineStart;
        span->endX = span->lastScanEndX = spanEnd - scanlineStart;
        span->y = y;
        span->endY = y+1;
        span->size = spanEnd - spanStart;
        span->next = span+1;
        ++span;
        ++numSpans;
      }
      else
      {
        ++x;
      }
    }
    y += yInc;
    scanline += scanlineInc;
    prevScanline += scanlineInc;
  }

  if (numSpans == 0)
    return 0;

  spans[numSpans-1].next = 0;
  return spans;
}

static Span* DiffFramebuffersToScanlineSpansExact(uint16_t *framebuffer, uint16_t *prevFramebuffer, bool interlacedDiff, int interlacedFieldParity)
{
  int numSpans = 0;
  int y = interlacedDiff ? interlacedFieldParity : 0;
  int yInc = interlacedDiff ? 2 : 1;
  // If doing an interlaced update, skip over every second scanline.
  int scanlineInc = interlacedDiff ? frameScanlineStrideBytes : (frameScanlineStrideBytes>>1);
  int scanlineEndInc = scanlineInc - frameWidth;
  uint16_t *scanline = framebuffer + y*(frameScanlineStrideBytes>>1);
  uint16_t *prevScanline = prevFramebuffer + y*(frameScanlineStrideBytes>>1); // (same scanline from previous frame, not preceding scanline)

  Span* head = 0;
  while(y < frameHeight)
  {
    uint16_t *scanlineStart = scanline;
    uint16_t *scanlineEnd = scanline + frameWidth;
    while(scanline < scanlineEnd)
    {
      uint16_t *spanStart;
      uint16_t *spanEnd;
      int numConsecutiveUnchangedPixels = 0;

      if (scanline + 1 < scanlineEnd)
      {
        uint32_t diff = (*(uint32_t *)scanline) ^ (*(uint32_t *)prevScanline);
        scanline += 2;
        prevScanline += 2;

        if (diff == 0) // Both 1st and 2nd pixels are the same
          continue;

        if (diff & 0xFFFF == 0) // 1st pixels are the same, 2nd pixels are not
        {
          spanStart = scanline - 1;
          spanEnd = scanline;
        }
        else // 1st pixels are different
        {
          spanStart = scanline - 2;
          if ((diff & 0xFFFF0000u) != 0) // 2nd pixels are different?
          {
            spanEnd = scanline;
          }
          else
          {
            spanEnd = scanline - 1;
            numConsecutiveUnchangedPixels = 1;
          }
        }

        // We've found a start of a span of different pixels on this scanline, now find where this span ends
        while(scanline < scanlineEnd)
        {
          if (*scanline++ != *prevScanline++)
          {
            spanEnd = scanline;
            numConsecutiveUnchangedPixels = 0;
          }
          else
          {
            if (++numConsecutiveUnchangedPixels > SPAN_MERGE_THRESHOLD)
              break;
          }
        }
      }
      else // handle the single last pixel on the row
      {
        if (*scanline++ == *prevScanline++)
          break;

        spanStart = scanline - 1;
        spanEnd = scanline;
      }

      // Submit the span update task
      Span *span = spans + numSpans;
      span->x = spanStart - scanlineStart;
      span->endX = span->lastScanEndX = spanEnd - scanlineStart;
      span->y = y;
      span->endY = y+1;
      span->size = spanEnd - spanStart;
      if (numSpans > 0) span[-1].next = span;
      else head = span;
      span->next = 0;
      ++numSpans;
    }
    y += yInc;
    scanline += scanlineEndInc;
    prevScanline += scanlineEndInc;
  }
  return head;
}

static void MergeScanlineSpanList(Span *listHead)
{
  for(Span *i = listHead; i; i = i->next)
  {
    Span *prev = i;
    for(Span *j = i->next; j; j = j->next)
    {
      // If the spans i and j are vertically apart, don't attempt to merge span i any further, since all spans >= j will also be farther vertically apart.
      // (the list is nondecreasing with respect to Span::y)
      if (j->y > i->endY) break;

      // Merge the spans i and j, and figure out the wastage of doing so
      int x = MIN(i->x, j->x);
      int y = MIN(i->y, j->y);
      int endX = MAX(i->endX, j->endX);
      int endY = MAX(i->endY, j->endY);
      int lastScanEndX = (endY > i->endY) ? j->lastScanEndX : ((endY > j->endY) ? i->lastScanEndX : MAX(i->lastScanEndX, j->lastScanEndX));
      int newSize = (endX-x)*(endY-y-1) + (lastScanEndX - x);
      int wastedPixels = newSize - i->size - j->size;
      if (wastedPixels <= SPAN_MERGE_THRESHOLD
#ifdef MAX_SPI_TASK_SIZE
        && newSize*SPI_BYTESPERPIXEL <= MAX_SPI_TASK_SIZE
#endif
      )
      {
        i->x = x;
        i->y = y;
        i->endX = endX;
        i->endY = endY;
        i->lastScanEndX = lastScanEndX;
        i->size = newSize;
        prev->next = j->next;
        j = prev;
      }
      else // Not merging - travel to next node remembering where we came from
        prev = j;
    }
  }
}

Span* ComputeDiff(
  int width,
  int height,
  int scanlineStrideBytes,
  uint16_t *framebuffer,
  uint16_t *prevFramebuffer,
  bool changed,
  bool interlacedDiff,
  int interlacedFieldParity
) {
  frameWidth = width;
  frameHeight = height;
  frameScanlineStrideBytes = scanlineStrideBytes;

#if defined(ALL_TASKS_SHOULD_DMA) && defined(UPDATE_FRAMES_WITHOUT_DIFFING)
  return NoDiffChangedRectangle();
#elif defined(ALL_TASKS_SHOULD_DMA) && defined(UPDATE_FRAMES_IN_SINGLE_RECTANGULAR_DIFF)
  return DiffFramebuffersToSingleChangedRectangle(framebuffer, prevFramebuffer);
#else
  if (!changed) {
    return 0;
  }
  // Collect all spans in this image
  Span* head;
#ifdef FAST_BUT_COARSE_PIXEL_DIFF
  // If possible, utilize a faster 4-wide pixel diffing method
  if (frameWidth % 4 == 0 && frameScanlineStrideBytes % 8 == 0)
    head = DiffFramebuffersToScanlineSpansFastAndCoarse4Wide(framebuffer, prevFramebuffer, interlacedDiff, interlacedFieldParity);
  else
#endif
    head = DiffFramebuffersToScanlineSpansExact(framebuffer, prevFramebuffer, interlacedDiff, interlacedFieldParity); // If disabled, or framebuffer width is not compatible, use the exact method

  // Merge spans together on adjacent scanlines - works only if doing a progressive update
  if (!interlacedDiff) {
    MergeScanlineSpanList(head);
  }
  return head;
#endif
}
