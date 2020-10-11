#pragma once

// Polls and saves the state of the battery. No-op if the function was called
// less than LOW_BATTERY_POLLING_INTERVAL tick() ago.
void PollLowBattery();

// Returns the most recent polled battery state. Always false if the project
// was compiled without a battery polling pin.
bool IsLowBattery();
