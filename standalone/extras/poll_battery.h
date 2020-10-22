#pragma once

// This functions must be called during the startup of the program to initialize
// the pin used for battery polling.
void InitPollBatterySystem();

// Polls and saves the state of the battery. No-op if the function was called
// less than LOW_BATTERY_POLLING_INTERVAL tick() ago.
void PollBattery();

// Returns the most recent polled battery state. Always false if the project
// was compiled without a battery polling pin.
bool IsLowBattery();
