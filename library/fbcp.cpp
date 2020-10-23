#include <stdio.h>

#include "config.h"
#include "include/fbcp.h"
#include "spi.h"
#include "display.h"
#include "diff.h"
#include "mailbox.h"
#include "extras/low_battery.h"
#include "extras/statistics.h"
#include "../throttle_usleep.h"
#include "../util.h"

typedef enum {
  STATE_INIT,
  STATE_OPEN,
  STATE_CLOSING,
  STATE_CLOSED
} State;

static State curState = STATE_INIT;
static BatteryState curBattery = BATTERY_OK;
static BacklightState curBacklight = BACKLIGHT_ON;

static uint32_t curFrameEnd;
static uint32_t prevFrameEnd;

static void SendBacklight() {
  if (curBacklight == BACKLIGHT_ON) {
    TurnDisplayOn();
  } else {
    TurnDisplayOff();
  }
}

extern void fbcp_open() {
  if (curState == STATE_INIT) {
    InitLowBatterySystem();
    InitDiff(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  } else if (curState != STATE_CLOSED) {
    LOG("Attempted to call fbcp_open when already open");
    return;
  }

  OpenMailbox();
  InitSPI();
  curFrameEnd = spiTaskMemory->queueTail;
  prevFrameEnd = spiTaskMemory->queueTail;
  SendBacklight();
  curState = STATE_OPEN;
}

extern void fbcp_close() {
  if (curState != STATE_OPEN) {
    LOG("Attempted to call fbcp_close when not open");
    return;
  }

  curState = STATE_CLOSING;
  TurnDisplayOff();
  DeinitSPI();
  CloseMailbox();
  curState = STATE_CLOSED;
}

extern void fbcp_block_until_ready() {
  if (curState != STATE_OPEN) {
    LOG("Attempted to call fbcp_block_until_ready when not open");
    return;
  }

  // At all times keep at most two rendered frames in the SPI task queue pending to be displayed. Only proceed to submit a new frame
  // once the older of those has been displayed.
  while (
    (spiTaskMemory->queueTail + SPI_QUEUE_SIZE - spiTaskMemory->queueHead) % SPI_QUEUE_SIZE >
    (spiTaskMemory->queueTail + SPI_QUEUE_SIZE - prevFrameEnd) % SPI_QUEUE_SIZE
  ) {
    // Peek at the SPI thread's workload and throttle a bit if it has got a lot of work still to do.
    double usecsUntilSpiQueueEmpty = spiTaskMemory->spiBytesQueued * spiUsecsPerByte;
    if (usecsUntilSpiQueueEmpty > 0)
    {
      uint32_t sleepUsecs = (uint32_t)(usecsUntilSpiQueueEmpty * 0.4);
      if (sleepUsecs > 1000) throttle_usleep(500);
    }
  }
}

extern void fbcp_mark_frame_end() { // temporary
  prevFrameEnd = curFrameEnd;
  curFrameEnd = spiTaskMemory->queueTail;
}

extern void fbcp_draw_overlay(
  uint16_t* data,
  int width,
  int height,
  int strideBytes
) {
  if (curState != STATE_OPEN) {
    LOG("Attempted to call fbcp_draw_overlay when not open");
    return;
  }

  Framebuffer framebuffer = {
    .data = data,
    .width = width,
    .height = height,
    .strideBytes = strideBytes
  };

  DrawStatisticsOverlay(framebuffer);
  if (curBattery == BATTERY_LOW) {
    DrawLowBatteryIcon(framebuffer);
  }
}

extern void fbcp_set_battery_indicator(BatteryState state) {
  curBattery = state;
}

extern void fbcp_set_backlight(BacklightState state) {
  if (curBacklight == state) {
    return;
  }
  curBacklight = state;
  if (curState == STATE_OPEN) {
    SendBacklight();
  }
}

extern int fbcp_get_display_width() {
  return DISPLAY_WIDTH;
}

extern int fbcp_get_display_height() {
  return DISPLAY_HEIGHT;
}

extern int fbcp_get_data_control_pin() {
#if defined(GPIO_TFT_DATA_CONTROL)
  return GPIO_TFT_DATA_CONTROL;
#else
  return FBCP_NO_PIN;
#endif
}

extern int fbcp_get_reset_pin() {
#if defined(GPIO_TFT_RESET_PIN)
  return GPIO_TFT_RESET_PIN;
#else
  return FBCP_NO_PIN;
#endif
}

extern int fbcp_get_backlight_pin() {
#if defined(GPIO_TFT_BACKLIGHT)
  return GPIO_TFT_BACKLIGHT;
#else
  return FBCP_NO_PIN;
#endif
}
