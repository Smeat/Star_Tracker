#include "config.h"
#include "esp32-hal-gpio.h"
#include "freertos/portmacro.h"
#include "stdint.h"
#define FROM_LIB

#include <Arduino.h>
#include "esp32-hal-timer.h"
#include "motor_controller.h"

void MotorController::initialize() {
#ifdef BOARD_ATMEGA    
    uint8_t pin_mask = (1 << STEP_PIN_DEC) | (1 << DIR_PIN_DEC) | (1 << MS_PIN_DEC) |
                       (1 << STEP_PIN_RA)  | (1 << DIR_PIN_RA)  | (1 << MS_PIN_RA);

    MOTORS_DDR |= pin_mask;
    MOTORS_PORT &= ~pin_mask;

    #ifdef DEBUG_OUTPUT
        Serial.println(F("Stepper motors pins initialized:"));
        Serial.print(F("  Pinout: ")); Serial.println(pin_mask, BIN);
        Serial.print(F("  DDR:    ")); Serial.println(MOTORS_DDR, BIN);
        Serial.print(F("  PORT:   ")); Serial.println(MOTORS_PORT, BIN);
    #endif

    // Timer/Counter Control Register: set Fast PWM mode
    TCCR5A = 0x23 ; // || set mode 7 (Fast PWM) with
    TCCR5B = 0x09 ; // || prescaler 1 (no prescaling)

    // Output Compare Register: set interrupt frequency
    OCR5A = TIMER_TOP - 1;
    OCR5B = 0;
      
    // Timer/Counter Interrupt Mask Register: set interrupt TIMERx_COMPA_vect
    TIMSK5 |= (1 << OCIE5A);

    #ifdef DEBUG_OUTPUT
        Serial.println(F("TimerX initialized."));
        Serial.print(F("  TCCRxA: ")); Serial.println(TCCR5A, BIN);
        Serial.print(F("  TCCRxB: ")); Serial.println(TCCR5B, BIN);
        Serial.print(F("  TIMSKx: ")); Serial.println(TIMSK5, BIN);
    #endif
#else
	pinMode(STEP_PIN_DEC, OUTPUT);
	pinMode(DIR_PIN_DEC, OUTPUT);
	pinMode(MS_PIN_DEC, OUTPUT);
	pinMode(STEP_PIN_RA, OUTPUT);
	pinMode(DIR_PIN_RA, OUTPUT);
	pinMode(MS_PIN_RA, OUTPUT);

#endif

    _commands = queue<command_t>(8);

    _dec_balance = 0;
    _ra_balance = 0;
	_motor_lock = xSemaphoreCreateMutex();
}

void MotorController::stop() {

	xSemaphoreTake(_motor_lock, portMAX_DELAY);
    #ifdef DEBUG_OUTPUT
        Serial.println(F("Stopping both motors:"));
    #endif
    
    cli();
    _dec.pulses_remaining = 0;
    _ra.pulses_remaining = 0;
    sei();

    #ifdef DEBUG_OUTPUT
        Serial.print(F("  DEC: ")); Serial.println(_dec.pulses_remaining);
        Serial.print(F("  RA:  ")); Serial.println(_ra.pulses_remaining);
    #endif
      
#ifdef BOARD_ATMEGA
    MOTORS_PORT &= ~((1 << STEP_PIN_DEC) | (1 << STEP_PIN_RA)); // step pins to LOW

    #ifdef DEBUG_OUTPUT
        Serial.print(F("  PORT:   ")); Serial.println(MOTORS_PORT, BIN);
    #endif
#else
	digitalWrite(STEP_PIN_DEC, LOW);
	digitalWrite(STEP_PIN_RA, LOW);
#endif

    _commands.clear();
	xSemaphoreGive(_motor_lock);
}

double MotorController::estimate_fast_turn_time(double revs_dec, double revs_ra) {

    int sd, sr;
    revs_to_steps(&sd, &sr, revs_dec, revs_ra, false);
    
    auto time_dec = estimate_motor_fast_turn_time(sd, ACCEL_STEPS_DEC, ACCEL_DELAY_DEC, FAST_DELAY_START_DEC, FAST_DELAY_END_DEC);
    auto time_ra  = estimate_motor_fast_turn_time(sr, ACCEL_STEPS_RA,  ACCEL_DELAY_RA,  FAST_DELAY_START_RA,  FAST_DELAY_END_RA);

    return max(time_dec, time_ra);
} 

double MotorController::estimate_motor_fast_turn_time(double steps, int accel_each, int accel_amount, int delay_start, int delay_end) {

    double time = 0;

    double total_steps = floor(steps);
    double accel_steps = floor(steps);
    int delay_curr = delay_start; 

    for (accel_steps -= accel_each; (accel_steps > steps / 2.0) && (delay_curr > delay_end);) {
        time += (double)delay_curr * accel_each;
        delay_curr -= accel_amount;
        accel_steps -= accel_each;
    }
    accel_steps += accel_each;

    return (2 * time + (2 * accel_steps - total_steps) * delay_curr) / 1000.0; 
}

void MotorController::fast_turn(double revs_dec, double revs_ra, boolean queueing) {
    turn_internal({revs_dec, revs_ra, FAST_DELAY_START_DEC, FAST_DELAY_START_RA, FAST_DELAY_END_DEC, FAST_DELAY_END_RA, false}, queueing);
}

void MotorController::slow_turn(double revs_dec, double revs_ra, double speed_dec, double speed_ra, boolean queueing) {
    // revolutions per second convert to delay in micros
    // there might be some overflows, but nobody cares ... (hopefully)
    uint32_t delay_dec = 1000000.0 / (speed_dec * STEPS_PER_REV_DEC * MICROSTEPPING_MUL);
    uint32_t delay_ra  = 1000000.0 / (speed_ra  * STEPS_PER_REV_RA  * MICROSTEPPING_MUL);
    turn_internal({revs_dec, revs_ra, delay_dec, delay_ra, delay_dec, delay_ra, true}, queueing);
}

void MotorController::turn_internal(command_t cmd, bool queueing) {
    if (queueing && !is_ready()) {
        _commands.push(cmd);
        return;
    }
	xSemaphoreTake(_motor_lock, portMAX_DELAY);
    int steps_dec, steps_ra;
    revs_to_steps(&steps_dec, &steps_ra, cmd.revs_dec, cmd.revs_ra, cmd.microstepping);
	log_d("turning by DEC %f RA %f revs, %d %d steps %d %d balance",cmd.revs_dec, cmd.revs_ra, steps_dec, steps_ra, this->_dec_balance, this->_ra_balance);
	log_d("total steps ra %d dec %d", _dec_balance, _ra_balance);
	
    //cli();

    // wait 1ms for pins to stabilize if needed
    bool dec_rev = cmd.revs_dec < 0;
	bool ra_rev = cmd.revs_ra < 0;


    int32_t effective_steps_dec = steps_dec;
    int32_t effective_steps_ra = steps_ra;

    _dec.pulses_to_accel = 0;
    _ra.pulses_to_accel  = 0;

    _dec.steps_total = effective_steps_dec;
    _ra.steps_total = effective_steps_ra;
    
    _dec.target_steps_delay = cmd.delay_end_dec;
    _ra.target_steps_delay  = cmd.delay_end_ra;

    _dec.start_steps_delay = cmd.delay_start_dec;
    _ra.start_steps_delay  = cmd.delay_start_ra;

    step_micros(&_dec, effective_steps_dec * 2, _dec.start_steps_delay, dec_rev);
    step_micros(&_ra,  effective_steps_ra  * 2, _ra.start_steps_delay, ra_rev);
        log_d("Initializing new movement.");
        log_d("  revs DEC:       %f", cmd.revs_dec);
        log_d("  resv RA:        %f", cmd.revs_ra);
		log_d("  steps DEC: %d RA: %d", effective_steps_dec, effective_steps_ra);
        log_d("  micro s. (t/f): %s", cmd.microstepping ? "enabled" : "disabled");
		log_d("Direction dec %d ra %d", digitalRead(DIR_PIN_DEC), digitalRead(DIR_PIN_RA));
	xSemaphoreGive(_motor_lock);

    // compensate coarse resolution of the full-step movement
    if (!cmd.microstepping) {
        double revs_dec, revs_ra;
        steps_to_revs(&revs_dec, &revs_ra, steps_dec - effective_steps_dec, steps_ra - effective_steps_ra, false);
        slow_turn(revs_dec, revs_ra, FAST_REVS_PER_SEC_DEC / MICROSTEPPING_MUL, FAST_REVS_PER_SEC_RA / MICROSTEPPING_MUL, true);
    }

#ifdef BOARD_ATMEGA
    TCNT1 = 0; // reset Timer1 counter
#endif

    //sei();
}

bool MotorController::change_pin(byte pin, byte value) {
#ifdef BOARD_ATMEGA
    if (((MOTORS_PORT >> pin) & 1) == value) return false;
    MOTORS_PORT ^= (-value ^ MOTORS_PORT) & (1 << pin);
#else
	uint8_t state = digitalRead(pin);
	if(state == value) return false;
	digitalWrite(pin, value);
	log_d("Changed pin %d to %d", pin, value);
#endif
    return true;
}

void MotorController::step_micros(motor_data* data, int pulses, unsigned long micros_between_steps, bool reverse) {
    data->pulses_remaining = pulses;
	data->reverse = reverse;
	if(micros_between_steps != data->current_steps_delay) log_d("Changed delay from %ul to %ul", data->current_steps_delay, micros_between_steps);
	data->current_steps_delay = micros_between_steps;
}

void MotorController::trigger() {
	xSemaphoreTake(_motor_lock, portMAX_DELAY);
	// All current commands are finished, so take the next from the queue
    if (_ra.pulses_remaining == 0 && _dec.pulses_remaining == 0 && _commands.count() > 0) {
		// TODO: never call turn_internal directly elsewhere. just use the queue
		xSemaphoreGive(_motor_lock);
        turn_internal(_commands.pop(), false);
		xSemaphoreTake(_motor_lock, portMAX_DELAY);
    }
    // DEC motor pulse should be done
    _dec_balance += motor_trigger(_dec, STEP_PIN_DEC, DIR_PIN_DEC, DIRECTION_DEC, MS_PIN_DEC);

    // RA motor pulse should be done
    _ra_balance += motor_trigger(_ra, STEP_PIN_RA, DIR_PIN_RA, DIRECTION_RA, MS_PIN_RA);

    // these calls will take some time so we will probaly miss some next 
    // interrupts but we do not really care because we are changing speed
    // and this does not happen during tracking so everything should be ok
    change_motor_speed(_dec, ACCEL_STEPS_DEC * 2, ACCEL_DELAY_DEC);
    change_motor_speed(_ra, ACCEL_STEPS_RA * 2, ACCEL_DELAY_RA);
	xSemaphoreGive(_motor_lock);
}

void MotorController::change_motor_speed(motor_data& data, unsigned int change_pulses, int amount) {

    bool accel_desired = false;
    bool decel_desired = false;

    if (data.pulses_to_accel >= change_pulses) {

        // are we at the middle of the motor movement?
        if (data.pulses_remaining > data.steps_total) {
            accel_desired = (data.current_steps_delay > data.target_steps_delay);
        }
        else if ((data.start_steps_delay - data.current_steps_delay) / amount >= data.pulses_remaining / change_pulses) {
            decel_desired = (data.current_steps_delay < data.start_steps_delay);
        }
 
        if (accel_desired || decel_desired) {
            int new_steps_delay = data.current_steps_delay - (accel_desired ? 1 : -1) * amount;
            step_micros(&data, data.pulses_remaining,  new_steps_delay, data.reverse); 
            // this procedure take some time (like 100 us) so we may compensate missed intrrupts somehow
        }

        data.pulses_to_accel = 0;
    }
}

int MotorController::motor_trigger(motor_data& data, byte step_pin, byte dir_pin, bool dir_swap, byte ms) {

    if (data.pulses_remaining == 0) return 0;
	data.inactive_us += TMR_RESOLUTION;
	if(data.inactive_us < data.current_steps_delay) return 0;

    ++data.pulses_to_accel;
    --data.pulses_remaining;

	data.inactive_us = 0;
#ifdef BOARD_ATMEGA
    MOTORS_PORT ^= (1 << step_pin);
    return (MOTORS_PORT & (1 << ms) ? 1 : MICROSTEPPING_MUL) * (((MOTORS_PORT >> dir_pin) & 1) != dir_swap ? -1 : 1);
#else
	// TODO: use proper xor
	digitalWrite(dir_pin, data.reverse);
	digitalWrite(step_pin, !digitalRead(step_pin));
	int8_t retval =  (digitalRead(ms) ? 1 : MICROSTEPPING_MUL) * (data.reverse ? -1 : 1);
	return retval;
#endif

}
