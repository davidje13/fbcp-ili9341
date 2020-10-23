#ifndef FBCP_H_INCLUDED
#define FBCP_H_INCLUDED

#include <inttypes.h>

extern "C" { // Static ABI

#define EXTERN_API extern __attribute__((visibility("default")))
#define CONST __attribute__((const))

#define FBCP_NO_PIN -1

// Initialise communication with the display and switch on the backlight.
EXTERN_API void fbcp_open();

// Stop communication with the display and switch off the backlight.
EXTERN_API void fbcp_close();

// Blocks the current thread until it is possible to submit a new frame.
// (keep at most two rendered frames in the SPI task queue pending to be
// displayed)
EXTERN_API void fbcp_block_until_ready();
EXTERN_API void fbcp_mark_frame_end(); // temporary

// Renders any overlays on the given frame (statistics / battery indicator).
EXTERN_API void fbcp_draw_overlay(
  uint16_t* data,
  int width,
  int height,
  int strideBytes
);

typedef enum {
  BATTERY_OK  = 0,
  BATTERY_LOW = 1
} BatteryState;

// Show or hide the "low battery" indicator on the display.
// If the library was compiled without low battery support, this is a no-op.
EXTERN_API void fbcp_set_battery_indicator(BatteryState state);

typedef enum {
  BACKLIGHT_OFF = 0,
  BACKLIGHT_ON  = 1
} BacklightState;

// Enable or disable the display backlight. If the library was compiled
// without a backlight pin, this is a no-op.
EXTERN_API void fbcp_set_backlight(BacklightState state);

// Return the physical pixel width of the display, set at compile time.
EXTERN_API CONST int fbcp_get_display_width();

// Return the physical pixel height of the display, set at compile time.
EXTERN_API CONST int fbcp_get_display_height();

// Return the GPIO pin number for Data/Control, or FBCP_NO_PIN (-1) if the
// library was compiled without a Data/Control pin.
EXTERN_API CONST int fbcp_get_data_control_pin();

// Return the GPIO pin number for Reset, or FBCP_NO_PIN (-1) if the library
// was compiled without a Reset pin.
EXTERN_API CONST int fbcp_get_reset_pin();

// Return the GPIO pin number for the backlight, or FBCP_NO_PIN (-1) if the
// library was compiled without a backlight control pin.
EXTERN_API CONST int fbcp_get_backlight_pin();

#undef EXTERN_API
#undef CONST

}

#endif
