#include "LX200.h"

#include "../core/clock.h"
#include "../net/TCP.h"
#include "RTClib.h"
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
	uint8_t data_begin = 0; // used to skip unwanted " " sent by libindi :/
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
						double dec = mount_controller->get_global_mount_orientation().dec;
						int decH = dec;
						int decM = (dec -decH)*60;
						int decS = (((dec -decH)*60) - decM)*60;
						snprintf(return_msg, 128, "%+02d*%02d'%02d#", decH, abs(decM), abs(decS));
					}
					break;
				// site names
				case 'M':
				case 'N':
				case 'O':
				case 'P':
					snprintf(return_msg, 128, "%s#", "none");
					break;
				// latitude
				case 't':
					{
						double latitude = LATITUDE;
						int deg = latitude;
						int min = (latitude - deg) * 60;
						snprintf(return_msg, 128, "%+02d*%02d#", deg, min);
						break;
					}
				// longitude
				case 'g':
					{
						double latitude = LONGITUDE;
						int deg = latitude;
						int min = (latitude - deg) * 60;
						snprintf(return_msg, 128, "%+03d*%02d#", deg, min);
						break;
					}
					break;
				// tracking rate TODO
				case 'T':
					snprintf(return_msg, 128, "60.0#");
					break;
				// UTC offset TODO
				case 'G':
					snprintf(return_msg, 128, "+00#");
					break;
				// current date
				case 'C':
					{
						DateTime curr_time = rt_clock->get_time();
						snprintf(return_msg, 128, "%02d/%02d/%02d", curr_time.month(), curr_time.day(), curr_time.year());
					}
					break;
				// firmware stuff. currently all TODO
				case 'V':
					switch(msg[3]) {
						case 'D':
							snprintf(return_msg, 128, "011 11 2020#");
							break;
						case 'N':
							snprintf(return_msg, 128, "11.1#");
							break;
						case 'P':
							snprintf(return_msg, 128, "DIY#");
							break;
						case 'T':
							snprintf(return_msg, 128, "11:11:11#");
							break;
						case 'F':
							// undocumented :/ seems to be for a full version string
							snprintf(return_msg, 128, "full version#");
							break;
					}
					
				default:
					break;
			}
		break; // end case 'G'
		case 'S':
			data_begin = 3 + (msg[3] == ' '); // skip unwanted space XXX: only valid for one char commands
			switch(msg[2]) {
				case 'r':
					{
						int hours = (msg[data_begin] - '0') * 10 + (msg[data_begin + 1] - '0');
						int minutes = (msg[data_begin + 3] - '0') * 10 + (msg[data_begin + 4] - '0');
						int seconds = (msg[data_begin + 6] - '0') * 10 + (msg[data_begin + 7] - '0');
						double ra = hours * 15 + minutes/4.0 + seconds/240.0;
						mount_controller->set_target_ra(ra);
						log_i("Moving ra to %f. %02d:%02d:%02d. msg was %s", ra, hours, minutes, seconds, msg);
						snprintf(return_msg, 128, "%d", 1);
					}
					break;
				case 'd':
					{
						int sign = (msg[data_begin] == '+') ? 1 : -1;
						int deg = (msg[data_begin + 1] - '0') * 10 + (msg[data_begin + 2] - '0');
						int minutes = (msg[data_begin + 4] - '0') * 10 + (msg[data_begin + 5] - '0');
						int seconds = (msg[data_begin + 7] - '0') * 10 + (msg[data_begin + 8] - '0');
						double dec = sign * (deg + minutes/60.0 + seconds/3600.0);
						mount_controller->set_target_dec(dec);
						log_i("Moving dec to %f. %+02d*%02d:%02d. msg was %s", dec, deg, minutes, seconds, msg);
						snprintf(return_msg, 128, "%d", 1);
					}
					break;
				// set date in stupid format MM/DD/YY
				case 'C':
					{
						int month = (msg[data_begin] - '0') * 10 + msg[data_begin + 1] - '0';
						int day = (msg[data_begin + 3] - '0') * 10 + msg[data_begin + 4] - '0';
						int year = 2000 + (msg[data_begin + 6] - '0') * 10 + msg[data_begin + 7] - '0';
						DateTime old_date = rt_clock->get_time();
						DateTime new_date(year, month, day, old_date.hour(), old_date.minute(), old_date.second());
						rt_clock->sync(new_date);
						log_i("New date is %d-%d-%d from msg %s", year, month, day, msg);
						// TODO: always valid for now
						// the string is part of the specification.....
						snprintf(return_msg, 128, "1Updating  Planetary Data#                                           #");
					}
				break;
				// time
				case 'L':
				{
					uint8_t hours = (msg[data_begin] - '0') * 10 + (msg[data_begin + 1] - '0');
					uint8_t minutes = (msg[data_begin + 3] - '0') * 10 + (msg[data_begin + 4] - '0');
					uint8_t seconds = (msg[data_begin + 6] - '0') * 10 + (msg[data_begin + 7] - '0');
					DateTime old_date = rt_clock->get_time();
					DateTime new_date(old_date.year(), old_date.month(), old_date.day(), hours, minutes, seconds);
					rt_clock->sync(new_date);
					log_i("New time is %d:%d:%d from msg %s", hours, minutes, seconds, msg);
					snprintf(return_msg, 128, "1");
				}
				break;
				// UTC offset TODO
				case 'G':
					snprintf(return_msg, 128, "1");
					break;
				// longitude
				case 'g':
				{
					uint8_t sign = msg[data_begin] == '+' ? 1 : -1;
					uint16_t degree = (msg[data_begin + 1] - '0') * 100 + (msg[data_begin + 2] - '0') * 10 + (msg[data_begin + 3] - '0');
					uint8_t minutes = (msg[data_begin + 5] - '0') * 10 + (msg[data_begin + 6] - '0');
					double longitude =sign * (degree + minutes/60.0);
					log_i("Setting longitude to %f", longitude);
					rt_clock->set_longitude(longitude);
					snprintf(return_msg, 128, "1");
				}
				break;
				// latitude TODO unused, but we never use it anyway
				case 't':
					snprintf(return_msg, 128, "1");
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
