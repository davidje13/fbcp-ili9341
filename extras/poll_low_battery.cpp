#include <string.h>

#include "../config.h"
#include "poll_low_battery.h"
#include "../gpu.h"
#include "../spi.h"

#ifdef LOW_BATTERY_PIN

static bool lowBattery = false;
static uint64_t lowBatteryLastPolled = 0;

void PollLowBattery()
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

void PollLowBattery() {}
bool IsLowBattery() { return false; }

#endif
