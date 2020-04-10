#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "config.h"
#include "control/control.h"
#include "control/LX200.h"

#include "core/motor_controller.h"

#include "core/camera_controller.h"
#include "core/canon_eos1000d.h"

#include "core/clock.h"
#include "core/rtc_ds3231.h"

#include "net/TCP.h"
#include "net/wireless.h"

#include <stdint.h>
#include <soc/timer_group_struct.h>
#include <soc/timer_group_reg.h>
#include <esp_task_wdt.h>

RtcDS3231 ds3231;
Clock& my_clock = ds3231;

CanonEOS1000D eos;
CameraController& camera = eos; 

MountController mount(MotorController::instance());

Control control(mount, camera, my_clock);

void watchdog_feed() {
  TIMERG0.wdt_wprotect = TIMG_WDT_WKEY_VALUE;
  TIMERG0.wdt_feed = 1;
  TIMERG0.wdt_wprotect = 0;
}

void tcp_task(void* param) {
	while(42) {
  		control.update();
  		tcp_update(lx200_handle_message);
		//uint8_t buf[] = ":Sr 10:10:10#";
//		lx200_handle_message(buf, strlen((char*)buf));
//		mount.set_target_dec(5);
//		mount.set_target_ra(5);
		vTaskDelay(10/portTICK_PERIOD_MS);
	}
}

void motor_task(void* param) {
	while(42) {
		MotorController::instance().trigger();
		vTaskDelay(1/portTICK_PERIOD_MS);
	}
}

void setup() {

  Serial.begin(SERIAL_BAUD_RATE);
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  Serial.println("Starting tcp, lx200 and wifi");
  log_e("TEST!");
  ESP_LOGI("HI", "ESP test!");
  lx200_init(&mount, &my_clock);
  initWifiAP();
  tcp_init();
  delay(10);
  control.initialize();
  delay(100);

  xTaskCreate(&tcp_task, "tcp_task", 18096, NULL, 5, NULL);
  xTaskCreate(&motor_task, "motor_task", 18096, NULL, 5, NULL);

}

void loop() {
//	watchdog_feed();
}
