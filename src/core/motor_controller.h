#ifndef MOTORCONTROLLER_H
#define MOTORCONTROLLER_H

#include "../config.h"
#include "./queue.h"
#include "stdint.h"

#define TIMER_TOP (F_CPU / (1000000.0 / TMR_RESOLUTION))

class MountController;
class MotorController {
    
    public:

        // singleton class
        MotorController(MotorController const&) = delete;
        void operator=(MotorController const&)  = delete;
        static MotorController& instance() {
            static MotorController instance;
            return instance;
        }
        
        // set PINs and default values
        void initialize();

        // returns true if motors have absolutely no job
        inline bool is_ready() { 
			xSemaphoreTake(_motor_lock, portMAX_DELAY);
			uint8_t retval = _dec.pulses_remaining == 0 && _ra.pulses_remaining == 0 && _commands.count() == 0; 
			xSemaphoreGive(_motor_lock);
			return retval;
		}


        // interrupts all motor movements and clear command queue
        void stop();

        // estimates time (millis) of the complete fast_turn duration
        double estimate_fast_turn_time(double revs_dec, double revs_ra);
        
        // make a fast turn with subsequent slow turn for compensate the coarse resolution of full step
        void fast_turn(double revs_dec, double revs_ra, boolean queueing);

        // make a turn with given motor revolutions per second and with microstepping enabled (implies low speed)
        void slow_turn(double revs_dec, double revs_ra, double speed_dec, double speed_ra, boolean queueing);

        // interrupt service rutine
        void trigger();

        // returns the number of revolutions relative to the starting position
        void get_made_revolutions(double& dec, double& ra) {
			xSemaphoreTake(_motor_lock, portMAX_DELAY);
                //log_d("Revolutions");
                //log_d(" DEC: %d", _dec_balance); 
                //log_d("  RA: %d", _ra_balance); 
            dec = (double) _dec_balance / 2.0 / STEPS_PER_REV_DEC / MICROSTEPPING_MUL;
            ra = (double) _ra_balance / 2.0 / STEPS_PER_REV_RA / MICROSTEPPING_MUL;
			xSemaphoreGive(_motor_lock);
        }

    private:
        MotorController() {}

        // structure holding state of motors and movement while executing a command
        struct motor_data {
             uint32_t steps_total = 0;  // steps to be done during this particular movement
             uint32_t pulses_remaining = 0;  // pulses to be done until the end of this movement
			 bool reverse = 0;
             uint32_t pulses_to_accel = 0;  // number of pulses after which is done an ac/deceleration
             uint32_t start_steps_delay = 0;  // delay between steps at the start of fast movement
             uint32_t target_steps_delay = 0;  // minimal delay between steps during fast movement
             uint32_t current_steps_delay = 0;  // current delay between steps
			 uint32_t ticks_passed = 0;
			 uint32_t inactive_us = 0;
        };

        // structre holding a command for motors
        struct command_t {
            double revs_dec;  // desired number of revolutions of DEC
            double revs_ra;  // desired number of revolutions of RA
            unsigned long delay_start_dec;  // starting delay between steps - DEC
            unsigned long delay_start_ra;  // starting delay between steps - RA
            unsigned long delay_end_dec;  // minimal delay between steps - DEC
            unsigned long delay_end_ra;  // minimal delay between steps - RA
            bool microstepping;  // whether enable microstepping
        };

        // estimates time (millis) of the complete fast_turn duration of a single motor
        double estimate_motor_fast_turn_time(double steps, int accel_each, int accel_amount, int dalay_start, int dalay_end);

        // make a turn of specified angles, speed (starting, ending) and command queueing
        void turn_internal(command_t cmd, bool queueing);

        // set job to move specified number of steps with delays between them
        void step_micros(motor_data* data, int steps, unsigned long micros_between_steps, bool rev);

        // performs acceleration or decceleration 'amount' if 'change_steps' passed
        inline void change_motor_speed(motor_data& data, unsigned int change_steps, int amount);

        // subrutine of the interrupt service rutine, returns microsteps which were done
        inline int motor_trigger(motor_data& data, byte pin, byte dir, bool dir_swap, byte ms);

        // returns true if 'value' is defferent from current value (1 or 0) and changes pin appropriately 
        inline bool change_pin(byte pin, byte value);

        inline void revs_to_steps(int* steps_dec, int* steps_ra, double revs_dec, double revs_ra, bool microstepping) {
            *steps_dec = abs(revs_dec) * STEPS_PER_REV_DEC * (microstepping ? MICROSTEPPING_MUL : 1);
            *steps_ra  = abs(revs_ra)  * STEPS_PER_REV_RA  * (microstepping ? MICROSTEPPING_MUL : 1);
        }

        inline void steps_to_revs(double* revs_dec, double* revs_ra, double steps_dec, double steps_ra, bool microstepping) {
            *revs_dec = steps_dec / STEPS_PER_REV_DEC / (microstepping ? MICROSTEPPING_MUL : 1);
            *revs_ra  = steps_ra  / STEPS_PER_REV_RA  / (microstepping ? MICROSTEPPING_MUL : 1);
        }

        // some motor state variables
        motor_data _dec;
        motor_data _ra;
        queue<command_t> _commands;

        long _dec_balance;
        long _ra_balance;
		SemaphoreHandle_t _motor_lock = NULL;
};

#ifdef BOARD_ATMEGA
#ifndef FROM_LIB
ISR(TIMER5_COMPA_vect) { MotorController::instance().trigger(); }
#endif
#endif

#endif
