#include "LX200.h"

#include "../core/clock.h"
#include "../net/TCP.h"

#include <cstdlib>
#include <stdio.h>
#include <string.h>

void lx200_init(MountController* mc) {
	mount_controller = mc;
}

void lx200_handle_message(uint8_t* msg, uint32_t len) {
	char return_msg[128];
	if(msg[0] != ':') return; // ignore invalid messages
	switch(msg[1]) {
		case 'G':
			switch(msg[2]) {
				// stupid time format no clue if we need that, but is is part of the spec
				case 'a':
					{
						DateTime time = Clock::get_time();
						snprintf(return_msg, 128, "%02d:%02d:%02d#", time.hour() % 12, time.minute(), time.second());
					}
					break;
				// proper time format
				case 'L':
					{
						DateTime time = Clock::get_time();
						snprintf(return_msg, 128, "%02d:%02d:%02d#", time.hour(), time.minute(), time.second());
					}
					break;
				case 'c':
					snprintf(return_msg, 128, "%d#", 24);
					break;
				// telescope RA in HH:MM:SS
				case 'R':
					{
						double ra = mount_controller->get_global_mount_orientation().ra;
						int raH = ra/15;
						int raM = ((ra/15.0) -raH)*60;
						int raS = ((((ra/15.0) -raH)*60) - raM)*60;
						snprintf(return_msg, 128, "%02d:%02d:%02d#", raH, raH, raS);
					}
					break;
				// telescope dec in HH:MM:SS
				case 'D':
					{
						double ra = mount_controller->get_global_mount_orientation().dec;
						int raH = ra/15;
						int raM = ((ra/15.0) -raH)*60;
						int raS = ((((ra/15.0) -raH)*60) - raM)*60;
						snprintf(return_msg, 128, "%02d:%02d:%02d#", raH, raH, raS);
					}
					break;
				default:
					break;
			}
		case 'S':
			switch(msg[2]) {
				case 'r':
					{
						char* pEnd;
						int hours = strtol((char*)msg + 3, &pEnd, 10);
						int minutes = strtol(pEnd, &pEnd, 10);
						int seconds = strtol(pEnd, &pEnd, 10);
						double ra = hours * 15 + minutes/4.0 + seconds/240.0;
						mount_controller->move_absolute_J2000(mount_controller->get_global_mount_orientation().dec, ra);
						snprintf(return_msg, 128, "%d#", 1);
					}
					break;
			}
		default:
			break;
	}

	Serial.printf("Got msg %s\n", msg);
	Serial.printf("Sending msg %s\n", return_msg);
	tcp_send_packet((uint8_t*)return_msg, strnlen(return_msg, 128));

}
