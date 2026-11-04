#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scans the 4x4 keypad in its own task and checks PINs against pin_store. A
   correct PIN asks system_logic to open the door, the same way as the console
   "unlock": it does not wait for an approval. It never touches the lock itself.
   Needs NVS and the default event loop. Does nothing if the keypad is turned
   off in menuconfig. */
esp_err_t keypad_input_start(void);

/* Ends a running lockout. For when the owner changes the PIN. Safe from any
   task. */
void keypad_input_clear_lockout(void);

#ifdef __cplusplus
}
#endif
