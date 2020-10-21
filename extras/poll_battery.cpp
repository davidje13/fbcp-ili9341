#include <string.h>

#include "../config.h"
#include "poll_battery.h"
#include "../tick.h"
#include "../spi.h" // gpio functions

#ifdef LOW_BATTERY_PIN

static bool lowBattery = false;
static uint64_t lowBatteryLastPolled = 0;

void InitPollBatterySystem()
{
  SET_GPIO_MODE(LOW_BATTERY_PIN, 0x00);
  PollBattery();
}

void PollBattery()
{
  uint64_t now = tick();
  if (now - lowBatteryLastPolled > LOW_BATTERY_POLLING_INTERVAL)
  {
    lowBattery = GET_GPIO(LOW_BATTERY_PIN) ? LOW_BATTERY_IS_ACTIVE_HIGH : !LOW_BATTERY_IS_ACTIVE_HIGH;
    lowBatteryLastPolled = now;
  }
}

bool IsLowBattery()
{
  return lowBattery;
}

#else

void InitPollBatterySystem() {}
void PollBattery() {}
bool IsLowBattery() { return false; }

#endif
