#ifndef FBCP_GPIO_H_INCLUDED
#define FBCP_GPIO_H_INCLUDED

#include <inttypes.h>

extern "C" { // Static ABI

#define EXTERN_API extern __attribute__((visibility("default")))

typedef enum {
  GPIO_MODE_INPUT  = 0b000,
  GPIO_MODE_OUTPUT = 0b001,
  GPIO_MODE_ALT0   = 0b100,
  GPIO_MODE_ALT1   = 0b101,
  GPIO_MODE_ALT2   = 0b110,
  GPIO_MODE_ALT3   = 0b111,
  GPIO_MODE_ALT4   = 0b011,
  GPIO_MODE_ALT5   = 0b010
} GPIOPinMode;

// Set pin mode
#define SET_GPIO_MODE(pin, mode) (gpio->gpfsel[(pin)/10] = (gpio->gpfsel[(pin)/10] & ~(0x7 << ((pin) % 10) * 3)) | ((mode) << ((pin) % 10) * 3))

// Get current pin state (0 = low, non-zero = high) - Pin must be (0-31)
#define GET_GPIO(pin) (gpio->gplev[0] & (1 << (pin)))

// Set pin high - Pin must be (0-31)
#define SET_GPIO(pin) (gpio->gpset[0] = 1 << (pin))

// Set pin low - Pin must be (0-31)
#define CLEAR_GPIO(pin) (gpio->gpclr[0] = 1 << (pin))

// Internal variables (do not use directly)
typedef struct GPIORegisterFile
{
  uint32_t gpfsel[6], reserved0; // GPIO Function Select registers, 3 bits per pin, 10 pins in an uint32_t
  uint32_t gpset[2], reserved1; // GPIO Pin Output Set registers, write a 1 to bit at index I to set the pin at index I high
  uint32_t gpclr[2], reserved2; // GPIO Pin Output Clear registers, write a 1 to bit at index I to set the pin at index I low
  uint32_t gplev[2];
} GPIORegisterFile;
EXTERN_API volatile GPIORegisterFile* gpio; // from spi.cpp

#undef EXTERN_API

}

#endif
