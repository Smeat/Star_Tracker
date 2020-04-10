#include "LX200.h"

#include "../core/clock.h"
#include "../net/TCP.h"
#include "stdint.h"

#include <cstdlib>
#include <stdio.h>
#include <string.h>

void lx200_init(MountController* mc) {
	mount_controller = mc;
}

static void lx200_handle_single_message(uint8_t* msg, uint32_t len) {
	char return_msg[128];
	return_msg[0] = 0;
	if(len == 1 && msg[0] == 0x06) {
		return_msg[0] = 'A';
		return_msg[1] = 0;
		goto lx200_end;
	} else if(msg[0] != ':' && msg[len-1] != '#'){
		log_d("Ignoring invalid msg: %s", msg);
		return; // ignore invalid messages
	}
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
				// FIXME: DEC and RA seem to be switched in KStars
				// telescope RA in HH:MM:SS
				case 'D':
					{
						//double ra = mount_controller->get_global_mount_orientation().ra;
						double ra = mount_controller->get_target().ra;
						int raH = ra/15;
						int raM = ((ra/15.0) -raH)*60;
						int raS = ((((ra/15.0) -raH)*60) - raM)*60;
						snprintf(return_msg, 128, "%02d:%02d:%02d#", raH, raM, raS);
					}
					break;
				// telescope dec in HH:MM:SS
				case 'R':
					{
						//double ra = mount_controller->get_global_mount_orientation().dec;
						double ra = mount_controller->get_target().dec;
						int raH = ra/15;
						int raM = ((ra/15.0) -raH)*60;
						int raS = ((((ra/15.0) -raH)*60) - raM)*60;
						snprintf(return_msg, 128, "+%02d*%02d'%02d#", raH, raM, raS);
					}
					break;
				default:
					break;
			}
		case 'S':
			switch(msg[2]) {
				case 'r':
					{
						int hours = (msg[4] - '0') * 10 + (msg[5] - '0');
						int minutes = (msg[7] - '0') * 10 + (msg[8] - '0');
						int seconds = (msg[10] - '0') * 10 + (msg[11] - '0');
						double ra = hours * 15 + minutes/4.0 + seconds/240.0;
						mount_controller->set_target_ra(ra);
						log_i("Moving ra to %f. %02d:%02d:%02d. msg was %s", ra, hours, minutes, seconds, msg);
						snprintf(return_msg, 128, "%d#", 1);
					}
					break;
				case 'd':
					{
						int sign = (msg[4] == '+') ? 1 : -1;
						int hours = (msg[5] - '0') * 10 + (msg[6] - '0');
						int minutes = (msg[8] - '0') * 10 + (msg[9] - '0');
						int seconds = (msg[11] - '0') * 10 + (msg[12] - '0');
						double dec = sign * hours * 15 + minutes/4.0 + seconds/240.0;
						mount_controller->set_target_dec(dec);
						log_i("Moving dec to %f. %02d:%02d:%02d. msg was %s", dec, hours, minutes, seconds, msg);
						snprintf(return_msg, 128, "%d#", 1);
					}
					break;
			}
		default:
			break;
	}

lx200_end:
	log_i("Got msg %s\n", msg);
	log_i("Sending msg %s\n", return_msg);
	tcp_send_packet((uint8_t*)return_msg, strnlen(return_msg, 128));
	heap_caps_check_integrity_all(true);
}

void lx200_handle_message(uint8_t* buf, uint32_t size) {
	for(uint32_t i = 0; i < size; ++i) {
		if(buf[i] == '#' || buf[i] == 0x06) {
			i += 1; // include #
			lx200_handle_single_message(buf, i);
			// We move the buf pointer and adjust the size, so we can begin from i=0 again
			buf = buf + i;
			size -= i;
			i = 0;
		}
	}
}
