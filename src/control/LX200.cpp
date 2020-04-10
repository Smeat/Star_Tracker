#include "LX200.h"

#include "../core/clock.h"
#include "../net/TCP.h"
#include "stdint.h"

#include <cstdlib>
#include <stdio.h>
#include <string.h>

static MountController* mount_controller = NULL;
static Clock* rt_clock = NULL;

void lx200_init(MountController* mc, Clock* c) {
	mount_controller = mc;
	rt_clock = c;
}

static void lx200_handle_single_message(uint8_t* msg, uint32_t len) {
	char return_msg[128];
	bool no_return = false;
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
				// telescope RA in HH:MM:SS
				case 'R':
				case 'r':
					{
						double ra = mount_controller->get_global_mount_orientation().ra;
						//double ra = mount_controller->get_target().ra;
						int raH = ra/15;
						int raM = ((ra/15.0) -raH)*60;
						int raS = ((((ra/15.0) -raH)*60) - raM)*60;
						snprintf(return_msg, 128, "%02d:%02d:%02d#", raH, raM, raS);
					}
					break;
				// telescope dec in DD*MM'SS
				case 'D':
				case 'd':
					{
						double ra = mount_controller->get_global_mount_orientation().dec;
						//double ra = mount_controller->get_target().dec;
						int raH = ra;
						int raM = (ra -raH)*60;
						int raS = (((ra -raH)*60) - raM)*60;
						snprintf(return_msg, 128, "+%02d*%02d'%02d#", raH, raM, raS);
					}
					break;
				// site names
				case 'M':
				case 'N':
				case 'O':
				case 'P':
					snprintf(return_msg, 128, "%s#", "none");
					break;
				// TODO: latitude
				case 't':
					{
						double latitude = LATITUDE;
						int deg = latitude;
						int min = (latitude - deg) * 60;
						snprintf(return_msg, 128, "%+02d*%02d#", deg, min);
						break;
					}
				case 'g':
					{
						double latitude = LONGITUDE;
						int deg = latitude;
						int min = (latitude - deg) * 60;
						snprintf(return_msg, 128, "%+03d*%02d#", deg, min);
						break;
					}
					break;

					
				default:
					break;
			}
		break; // end case 'G'
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
						snprintf(return_msg, 128, "%d", 1);
					}
					break;
				case 'd':
					{
						int sign = (msg[4] == '+') ? 1 : -1;
						int deg = (msg[5] - '0') * 10 + (msg[6] - '0');
						int minutes = (msg[8] - '0') * 10 + (msg[9] - '0');
						int seconds = (msg[11] - '0') * 10 + (msg[12] - '0');
						double dec = sign * (deg + minutes/60.0 + seconds/3600.0);
						mount_controller->set_target_dec(dec);
						log_i("Moving dec to %f. %+02d*%02d:%02d. msg was %s", dec, deg, minutes, seconds, msg);
						snprintf(return_msg, 128, "%d", 1);
					}
					break;
				// set date in stupid format MM/DD/YY
				case 'C':
					{
						int month = (msg[4] - '0' * 10) + msg[5] - '0';
						int day = (msg[7] - '0' * 10) + msg[8] - '0';
						int year = 2000 + (msg[10] - '0' * 10) + msg[11] - '0';
						DateTime old_date = rt_clock->get_time();
						DateTime new_date(year, month, day, old_date.hour(), old_date.minute(), old_date.second());
						rt_clock->sync(new_date);
						log_i("New date is %d-%d-%d", year, month, day);
						// TODO: always valid for now
						// the string is part of the specification.....
						snprintf(return_msg, 128, "1Updating  Planetary Data#                                           #");
					}
				break;
			}
		break; // end case 'S'
		case 'M':
			switch(msg[2]) {
				case 'S':
					snprintf(return_msg, 128, "%d", 0);
					break;
				default:
					break;
			}
		// TODO: implement distance bars
		case 'D':
			snprintf(return_msg, 128, "#");
			break;
		// stop command
		case 'Q':
			switch(msg[2]) {
				case '#':
				// TODO: implement directional stop
				case 'e':
				case 'n':
				case 's':
				case 'w':
					mount_controller->stop_all();
					no_return = true;
					break;
			}
		// use Autostar responses for now
		case 'L':
			switch(msg[2]) {
				case 'f':
					snprintf(return_msg, 128, "0 - Objects found#");
					break;
				case 'I':
					snprintf(return_msg, 128, "M31#");
					break;
			}
		default:
			break;
	}

lx200_end:
	log_i("Got msg %s\n", msg);
	log_i("Sending msg %s\n", return_msg);
	if(strnlen(return_msg, 128) == 0 && !no_return) log_w("##### UNKNONW MESSAGE %s ######", msg);
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
