#ifndef __LX200_H
#define __LX200_H

#include <Arduino.h>
#include <stdint.h>
#include "../core/mount_controller.h"

static MountController* mount_controller = NULL;

void lx200_init(MountController* controller);
void lx200_handle_message(uint8_t* message, uint32_t len);

#endif // __LX200_H
