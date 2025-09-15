#include "sdwrap.h"
#include <systemd/sd-daemon.h>

int sdw_watchdog_enabled(uint64_t *usec){return sd_watchdog_enabled(0,usec);}
void sdw_notify_ready(void){sd_notify(0, "READY=1");}

void sdw_notify_watchdog(void){sd_notify(0, "WATCHDOG=1");}

void sdw_notify_stopping(void){ sd_notify(0, "STOPPING=1");}
