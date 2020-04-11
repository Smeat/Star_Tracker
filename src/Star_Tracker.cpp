#include "esp_attr.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "config.h"
#include "control/control.h"
#include "control/LX200.h"

#include "core/motor_controller.h"
#include "core/mount_controller.h"

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

void watchdog_add_task() {
	esp_task_wdt_add(NULL);
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

void IRAM_ATTR motor_task(void* param) {
//	watchdog_add_task();
	uint8_t state = 0;
	while(42) {
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		digitalWrite(33, state);
		state = !state;
		MotorController::instance().trigger();
//		watchdog_feed();
	}
}

hw_timer_t* motor_timer = NULL;
static TaskHandle_t motor_task_handle = NULL;

void IRAM_ATTR motor_isr() {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	vTaskNotifyGiveFromISR(motor_task_handle, &xHigherPriorityTaskWoken);
	if(xHigherPriorityTaskWoken) {
		portYIELD_FROM_ISR();
	}
}

void info_task(void*) {
	while(42) {
//		mount.get_global_mount_orientation();

		log_v("Moving to 90,0");
		mount.move_absolute(90, 0);
		log_v("done!");
		// FIXME: timing problem. if delay is too small I don't reach the target
		// seems to be another problem
		do {
			log_v("Waiting for mount controller...");
			vTaskDelay(10000/portTICK_PERIOD_MS);
		} while(mount.is_moving());
		log_v("Moving to 0,0");
		mount.move_absolute(0, 0);
		do {
			log_v("Waiting for mount controller...");
			vTaskDelay(10000/portTICK_PERIOD_MS);
		} while(mount.is_moving());
	}
}

void setup() {
	pinMode(33, OUTPUT);
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

  xTaskCreatePinnedToCore(&tcp_task, "tcp_task", 18096, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(&info_task, "info_task", 8096, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(&motor_task, "motor_task", 8096, NULL, 5, &motor_task_handle, 0);

  motor_timer = timerBegin(0, 80, true);
  timerAttachInterrupt(motor_timer, &motor_isr, true);
  timerAlarmWrite(motor_timer, 64, true);
  timerAlarmEnable(motor_timer);

}

void loop() {
//	watchdog_feed();
}
