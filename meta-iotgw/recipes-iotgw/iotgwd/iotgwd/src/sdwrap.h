#pragma once
#include <stdint.h>

// Return >0 if watchdog enabled, fill usec; 0 if not enabled; <0 an error
int sdw_watchdog_enabled(uint64_t *usec);

// Send READY=1 / WATCHDOG=1 / STOPPING=1
void sdw_notify_ready(void);
void sdw_notify_watchdog(void);
void sdw_notify_stopping(void);
