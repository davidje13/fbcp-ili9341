#ifndef FBCP_TICK_H_INCLUDED
#define FBCP_TICK_H_INCLUDED

#include <inttypes.h>

extern "C" { // Static ABI

#define EXTERN_API extern __attribute__((visibility("default")))

// Get microseconds elapsed since boot
#define tick() (*systemTimerRegister)

// Internal variables (do not use directly)
extern volatile uint64_t *systemTimerRegister; // from spi.cpp

#undef EXTERN_API

}

#endif
