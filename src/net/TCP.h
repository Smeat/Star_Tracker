#ifndef __TCP_H__
#define __TCP_H__

#include <Arduino.h>
#include <stdint.h>

void IRAM_ATTR tcp_send_packet(uint8_t* buf, uint32_t size);
void tcp_init();
void IRAM_ATTR tcp_update(void (*callback)(uint8_t* buf, uint32_t size));

#endif // __TCP_H__
