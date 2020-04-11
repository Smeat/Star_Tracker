#define FROM_LIB

#include <Arduino.h>
#include <float.h>

#include "mount_controller.h"

void MountController::initialize() {

    _motors.initialize();

    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("Stepper motors initialized."));
    #endif

    _is_tracking = false;
    
    _mount_orientation = {0, 0};
    set_mount_pole(coord_t {DEFAULT_POLE_DEC, DEFAULT_POLE_RA}, DEFUALT_RA_OFFSET);

    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("Mount initialized."));
    #endif
}

MountController::coord_t MountController::get_global_mount_orientation() {

    coord_t local = get_local_mount_orientation();
    coord_t global = polar_to_polar(local, _transition_inverse);

    // see _mount_pole comments in header file for the explanation of 180-...
    global.ra = to_time_global_ra(global.ra);
    if (global.ra < 0) global.ra += 360;

    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("Global orientation:"));
        Serial.print(F("  DEC:  ")); Serial.println(global.dec);
        Serial.print(F("  RA:   ")); Serial.println(global.ra);
    #endif

    return global;
}

MountController::coord_t MountController::get_local_mount_orientation() {

    double dec_revs_done, ra_revs_done;
    _motors.get_made_revolutions(dec_revs_done, ra_revs_done);

    _mount_orientation = revolutions_to_angle({dec_revs_done, ra_revs_done});

    // DEC and RA must be in bounds and this should never happen! exception would be wonderful 
    if (_mount_orientation.dec > 90.0 || _mount_orientation.dec < -90.0 ||
        _mount_orientation.ra > 360.0 || _mount_orientation.ra < 0.0){
        log_e("Weird things happed! DEC out of bounds!");
		log_e("DEC revs %f RA revs %f", dec_revs_done, ra_revs_done);
		log_e("Orientation dec %f RA %f", _mount_orientation.dec, _mount_orientation.ra);
    }   
   
    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("Local orientation:"));
        Serial.print(F("  DEC:  ")); Serial.println(_mount_orientation.dec, 7);
        Serial.print(F("  RA:   ")); Serial.println(_mount_orientation.ra, 7);
    #endif

    return _mount_orientation;
}

void MountController::all_star_alignment(coord_t kernel[], coord_t image[], uint8_t points_num) {

    // Should work similarly to Celestron All-star alignment
    // Simple evolutionary strategy for optimization, which is slow :(
    // TODO: better analytical solution

    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("All start alignment:"));
        Serial.print(F("# input points: "));
        Serial.println(points_num);
        for (int i = 0; i < points_num; ++i) {
            Serial.print(F("  "));
            Serial.print(kernel[i].ra, 2);
            Serial.print(F(","));
            Serial.print(kernel[i].dec, 2);
            Serial.print(F(" -> "));
            Serial.print(image[i].ra, 2);
            Serial.print(F(","));
            Serial.println(image[i].dec, 2);
        }
    #endif

    static const long rnd_max = 1000000;

    cartesian_t x[CAL_BUFFER_SIZE];
    cartesian_t y[CAL_BUFFER_SIZE];

    for (int i = 0; i < points_num; ++i) {
        x[i] = polar_to_cartesian(kernel[i]);
        y[i] = polar_to_cartesian(image[i]);
    }

    deg_t solution[3];
    solution[0] = random(0, 360 * rnd_max) / (double)rnd_max;
    solution[1] = random(-90 * rnd_max, 90 * rnd_max) / (double)rnd_max;
    solution[2] = random(0, 360 * rnd_max) / (double)rnd_max;
  
    double best_fitness = 0;
    double sigma = OPT_SIGMA;

    for (int s = 0; s < OPT_GENERAITONS; ++s) {

        deg_t best_offspring[3];

        for (size_t i = 0; i < OPT_POPULATION_SIZE; i++) {

            deg_t offspring[3];
            offspring[0] = solution[0] + random_normal() * sigma;
            offspring[1] = solution[1] + random_normal() * sigma;
            offspring[2] = solution[2] + random_normal() * sigma;

            matrix_t A = make_transition_matrix({offspring[1], offspring[0]}, offspring[2]);

            double objective = 0;
            for (uint8_t i = 0; i < points_num; ++i) {           
                cartesian_t p = A * x[i];
                double d_x = (p.x - y[i].x);
                double d_y = (p.y - y[i].y); 
                double d_z = (p.z - y[i].z); 
                objective += d_x * d_x + d_y * d_y + d_z * d_z;
            }

            double fitness = 1.0 / (objective + 1.0);

            if (best_fitness < fitness) {
                best_fitness = fitness;
                best_offspring[0] = offspring[0];
                best_offspring[1] = offspring[1];
                best_offspring[2] = offspring[2];
            }
        }

        solution[0] = best_offspring[0];
        solution[1] = best_offspring[1];
        solution[2] = best_offspring[2];
        
        #ifdef DEBUG_OUTPUT_MOUNT
            if (s % 25 == 0) {
                Serial.print(F("("));  Serial.print(s);
                Serial.print(F(") | Fitness: "));    Serial.print(best_fitness, 12);
                Serial.print(F(" | RA: "));     Serial.print(solution[0], 7);
                Serial.print(F(" | DEC: "));    Serial.print(solution[1], 7);
                Serial.print(F(" | Off: "));    Serial.println(solution[2], 7);
            }
        #endif

        if (best_fitness > OPT_PRECISION) break;
        sigma *= OPT_SIGMA_DECAY;
    }

    solution[0] = fmod(solution[0], 360);
    solution[1] = fmod(solution[1], 90);
    solution[2] = fmod(solution[2], 360);

    if (solution[0] < 0) solution[0] += 360;
    if (solution[2] < 0) solution[2] += 360;

    set_mount_pole(coord_t {solution[1], solution[0]}, solution[2]);
}

void MountController::move_absolute_J2000(deg_t angle_dec, deg_t angle_ra) {

    // Equations from Astrophysical Fomulae: Volume II page 18
    // accuracy of few arc seconds
    // it could be worth it to use an exact precession matrix like Stellarium does 

    if (angle_dec < -90 || angle_dec > 90 || angle_ra < 0 || angle_ra >= 360) return;

    double r = to_rad(angle_ra);
    double c = to_rad(angle_dec);
    
    auto dt = Clock::get_time();
    double t = (double)dt.secondstime() / 31557600.0 / 100.0;

    double M = to_rad(1.2812323 * t + 0.0003879 * t * t + 0.0000101 * t * t * t);
    double N = to_rad(0.5567530 * t - 0.0001185 * t * t - 0.0000116 * t * t * t);
    
    double r_m = r + 0.5 * (M + N * sinf(r) * tanf(c));
    double d_m = c + 0.5 * N * cosf(r_m);
        
    double r_c = r + M + N * sinf(r_m) * tanf(d_m);
    double d_c = c + N * cosf(r_m);

    r_c = fmod(r_c, 2 * PI);
    
    move_absolute(to_deg(d_c), to_deg(r_c));
}

void MountController::move_absolute(deg_t angle_dec, deg_t angle_ra) {

    if (angle_dec < -90 || angle_dec > 90 || angle_ra < 0 || angle_ra >= 360) {
		log_e("##### Invalid angle! dec %f, ra %f", angle_dec, angle_ra);
		return;
	}
    _motors.stop(); 
    
	log_d("trying to get data");
    coord_t target = polar_to_polar({angle_dec, to_time_global_ra(angle_ra)}, _transition);
    coord_t o = get_local_mount_orientation();
	log_d("Angle to res");
    
    coord_t revs = angle_to_revolutions({target.dec - o.dec, target.ra  - o.ra});
    double travel_time = _motors.estimate_fast_turn_time(revs.dec, revs.ra) / 1000.0 / 3600.0;

	log_d("polar to polar");
    target = polar_to_polar({angle_dec, to_future_global_ra(angle_ra, travel_time)}, _transition);
    revs = angle_to_revolutions({target.dec - o.dec, target.ra  - o.ra});

    //#ifdef DEBUG_OUTPUT_MOUNT
        log_d("Turning at high speed by:");
        log_d("       DEC:  %f", target.dec - o.dec);
        log_d("       RA:   %f", target.ra  - o.ra);
        log_d("  tran DEC:  %f --> %f", angle_dec, target.dec);
        log_d("  tran RA:   %f --> %f", angle_ra, target.ra);
        log_d("  revs DEC:  %f",revs.dec);
        log_d("  revs RA:   %f",revs.ra);
		log_d("from DEC %f RA %f to DEC %f RA %f", o.dec, o.ra, target.dec, target.ra);
    //#endif

    _motors.fast_turn(revs.dec, revs.ra, false);
}

void MountController::move_relative_local(deg_t angle_dec, deg_t angle_ra) {

    coord_t curr_pos = get_local_mount_orientation();

    angle_dec = fmod(angle_dec, 180);
    angle_ra  = fmod(angle_ra,  360);

    // DEC cannot exceed -90..90 degrees
    if (curr_pos.dec + angle_dec < -90) angle_dec = -90 - curr_pos.dec;
    else if (curr_pos.dec + angle_dec > 90) angle_dec = 90 - curr_pos.dec;

    // RA can, but should not exceed 0..360 because of wires etc.
    if (curr_pos.ra + angle_ra < 0) angle_ra = -curr_pos.ra;
    else if (curr_pos.ra + angle_ra > 360) angle_ra = 360 - curr_pos.ra;

    coord_t revs = angle_to_revolutions({angle_dec, angle_ra});

    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("Turning at high speed by:"));
        Serial.print(F("  DEC:  ")); Serial.println(angle_dec);
        Serial.print(F("  RA:   ")); Serial.println(angle_ra);
        Serial.print(F("  revs DEC:  ")); Serial.println(revs.dec, 7);
        Serial.print(F("  revs RA:   ")); Serial.println(revs.ra, 7);
    #endif
        
    _motors.fast_turn(revs.dec, revs.ra, false);
}

void MountController::move_relative_global(deg_t angle_dec, deg_t angle_ra) {

    angle_dec = to_180_range(fmod(angle_dec, 360));
    angle_ra  = to_180_range(fmod(angle_ra,  360));

    coord_t curr_pos = get_local_mount_orientation();  
    coord_t curr_global = polar_to_polar(curr_pos, _transition_inverse);

    // new desired global pos DEC can also change RA if exceeds bounds

    curr_global.dec += angle_dec;

    if (curr_global.dec > 90) {
        angle_ra += 180;
        curr_global.dec = 180 - curr_global.dec;
    } else
    if (curr_global.dec < -90) {
        angle_ra += 180;
        curr_global.dec = -180 - curr_global.dec;
    }

    curr_global.ra = fmod(curr_global.ra + angle_ra, 360);
    if (curr_global.ra < 0) curr_global.ra += 360;

    coord_t new_pos = polar_to_polar(curr_global, _transition);
    coord_t revs = angle_to_revolutions({new_pos.dec - curr_pos.dec, new_pos.ra - curr_pos.ra});
    
    double travel_time = _motors.estimate_fast_turn_time(revs.dec, revs.ra) / 1000.0 * 15.0 / 3600.0; 
    curr_global.ra = fmod(curr_global.ra + travel_time, 360);

    new_pos = polar_to_polar(curr_global, _transition);
    revs = angle_to_revolutions({new_pos.dec - curr_pos.dec, new_pos.ra - curr_pos.ra});

    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("Turning at high speed by:"));
        Serial.print(F("  DEC:  ")); Serial.println(new_pos.dec - curr_pos.dec);
        Serial.print(F("  RA:   ")); Serial.println(new_pos.ra  - curr_pos.ra);
        Serial.print(F("  revs DEC:  ")); Serial.println(revs.dec);
        Serial.print(F("  revs RA:   ")); Serial.println(revs.ra);
    #endif
        
    _motors.fast_turn(revs.dec, revs.ra, false);
}

void MountController::set_tracking() {

    // TODO: it would be nice to change the speed dynamically after some time, based on the time
    // ellapsed from the start of the movement. Unfortunately it would require a schedule 
    // of steppers speed and a clever correction (we are doing a conversion of a complex
    // sin/cos/sqrt function into a simple stair function) and it is hard.

    double w = 15.0;
    
    coord_t target = get_global_mount_orientation();
    coord_t speed = get_ra_speed_transform(w, 0.0, target, _mount_pole, _mount_ra_offset);

    #ifdef DEBUG_OUTPUT_MOUNT
        Serial.println(F("Tracking:"));
        Serial.print(F("  target DEC: ")); Serial.println(target.dec);
        Serial.print(F("  target RA:  ")); Serial.println(target.ra);
        Serial.print(F("  speed DEC (dps): ")); Serial.println(speed.dec / 3600.0, 7);  // 0.0 
        Serial.print(F("  speed RA (dps):  ")); Serial.println(speed.ra  / 3600.0, 7);  // 0.0041667
    #endif

    speed = angle_to_revolutions(speed);
    _motors.slow_turn(speed.dec, speed.ra, speed.dec / 3600.0, speed.ra / 3600.0, true);
    
    _is_tracking = true;
}

void MountController::set_parking() {

    double dec_revs_done, ra_revs_done;
    _motors.get_made_revolutions(dec_revs_done, ra_revs_done);
    
    _motors.fast_turn(-dec_revs_done, -ra_revs_done, false);
}

MountController::coord_t MountController::get_ra_speed_transform(deg_t ra_speed, double t, coord_t point, coord_t pole, deg_t ra_offset) {

    // Ugly and hardcoded derivative :-(

    //  1) take x, y, z of a point based on start point, time and angle velocity
    //  2) transform into z' (like using _transform), but matrices must be hardcoded because of the next step
    //  3) take derivative dz'/dt
    //  4) compute derivatives of dec angle velocity: w_dec = (dz'/dt) / sqrt(1 - z'^2)
    //  5) ra angle velocity w_ra^2 = 1 - w_dec^2

    double coscos = cos(to_rad(pole.dec)) * cos(to_rad(point.dec));
    double sinsin = sin(to_rad(pole.ra))  * sin(to_rad(point.dec));
    double real_ra = to_rad(point.ra - ra_offset + ra_speed * t);

    double z_transformed = sinsin - coscos * cos(real_ra);
    double z_derivative  = coscos * sin(real_ra);

    double w_dec = sqrt(1.0 - z_transformed * z_transformed);
    if (w_dec <= 0) w_dec = 0.0;
    else w_dec = z_derivative / w_dec; // arcsin derivative
    double w_ra = sqrt(1.0 - w_dec);

    return coord_t { ra_speed * w_dec, ra_speed * w_ra };
}

void MountController::stop_tracking() {

    if (!_is_tracking) return;

    _motors.stop();
    _is_tracking = false;
}

MountController::cartesian_t MountController::polar_to_cartesian(coord_t polar) {

    double rad_dec = to_rad(polar.dec);
    double rad_ra  = to_rad(polar.ra);
    double cos_dec = cos(rad_dec);

    return cartesian_t { cos_dec * cos(rad_ra),
                         cos_dec * sin(rad_ra),
                         sin(rad_dec) };
}

MountController::coord_t MountController::cartesian_to_polar(cartesian_t cartesian) {

    // TODO: this function should be rewritten to reach a better precision (asin)

    double ra = to_deg(atan2(cartesian.y, cartesian.x));
    if (ra < 0) ra += 360;
       
    return coord_t { to_deg(asin(cartesian.z)), ra};
}

MountController::matrix_t MountController::get_dec_transition(deg_t dec) {

    double cos_dec = cos(to_rad(dec));
    double sin_dec = sin(to_rad(dec));

    return matrix_t {
        {{ sin_dec, 0, -cos_dec },
         { 0      , 1,  0       },
         { cos_dec, 0,  sin_dec }}
    };
}

MountController::matrix_t MountController::get_dec_transition_inverse(deg_t dec) {

    double cos_dec = cos(to_rad(dec));
    double sin_dec = sin(to_rad(dec));
    
    return matrix_t {
        {{ sin_dec, 0, cos_dec},
         { 0      , 1, 0      },
         {-cos_dec, 0, sin_dec}}
    };
}

MountController::matrix_t MountController::get_ra_transition(deg_t ra) {

    double cos_ra = cos(to_rad(ra));
    double sin_ra = sin(to_rad(ra));

    return matrix_t {
        {{  cos_ra, sin_ra, 0},
         { -sin_ra, cos_ra, 0},
         {  0,      0,      1}}
    };
}

MountController::matrix_t MountController::get_ra_transition_inverse(deg_t ra) {

    double cos_ra = cos(to_rad(ra));
    double sin_ra = sin(to_rad(ra));

    return matrix_t {
        {{ cos_ra, -sin_ra, 0},
         { sin_ra,  cos_ra, 0},
         { 0,       0,      1}}
    };
}

double MountController::random_normal() {

    static const long rnd_max = 1000000;

    static double z1;
    static bool generate;
    generate = !generate;

    if (!generate) return z1;

    double u1, u2;
    do {
        u1 = random(0, rnd_max) / (double)rnd_max;
        u2 = random(0, rnd_max) / (double)rnd_max;
    }
	// TODO: do we need to change this, if the compiler only supports 32bit float?
    while (u1 <= DBL_MIN);

    double z0;
    double s = sqrt(-2.0 * log(u1));
    z0 = s * cos(2.0 * 3.14159265358 * u2);
    z1 = s * sin(2.0 * 3.14159265358 * u2);

    return z0;
}

void MountController::set_target_ra(double ra) {
	this->_current_target.ra = ra;
	heap_caps_check_integrity_all(true);
	this->move_absolute(this->_current_target.dec, this->_current_target.ra);
//	this->set_tracking();
}

void MountController::set_target_dec(double dec) {
	this->_current_target.dec = dec;
	heap_caps_check_integrity_all(true);
	this->move_absolute(this->_current_target.dec, this->_current_target.ra);
//	this->set_tracking();
}
