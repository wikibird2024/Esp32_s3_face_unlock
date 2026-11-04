#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shows one status word on the OLED for the person at the door, and the IP
   address for a few seconds when Wi-Fi connects. Listens to events only and
   never touches the lock.
   Needs the default event loop. Call it before Wi-Fi starts, or the first IP
   address is missed. It does not wait for the screen: all I2C work runs in the
   display task, so a stuck bus cannot stop the boot. A missing screen only logs
   a warning. */
esp_err_t display_start(void);

#ifdef __cplusplus
}
#endif
