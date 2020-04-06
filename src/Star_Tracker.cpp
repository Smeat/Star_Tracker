#include "config.h"
#include "control/control.h"

#include "core/motor_controller.h"

#include "core/camera_controller.h"
#include "core/canon_eos1000d.h"

#include "core/clock.h"
#include "core/rtc_ds3231.h"

RtcDS3231 ds3231;
Clock& clock = ds3231;

CanonEOS1000D eos;
CameraController& camera = eos; 

MountController mount(MotorController::instance());

Control control(mount, camera, clock);

void setup() {

  Serial.begin(SERIAL_BAUD_RATE);
  delay(10);
  control.initialize();
  delay(100);

}

void loop() {

  control.update();
  delay(10);

}
