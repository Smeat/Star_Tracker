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

RtcDS3231 ds3231;
Clock& rt_clock = ds3231;

CanonEOS1000D eos;
CameraController& camera = eos; 

MountController mount(MotorController::instance());

Control control(mount, camera, rt_clock);

void setup() {

  Serial.begin(SERIAL_BAUD_RATE);
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  Serial.println("Starting tcp, lx200 and wifi");
  log_e("TEST!");
  ESP_LOGI("HI", "ESP test!");
  lx200_init(&mount);
  initWifiAP();
  tcp_init();
  delay(10);
  control.initialize();
  delay(100);

}

void loop() {

  control.update();
  delay(10);

}
