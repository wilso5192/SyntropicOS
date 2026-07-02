/* ═══════════════════════════════════════════════════════════════════════════
 * PLANT CONFIGURATIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char     *name;
    SimPlantParams  params;
    double          start_m;         /* starting position on track */
    double          move_dist_m;     /* trajectory distance */
    double          cruise_mps;      /* max cruise velocity */
    double          accel_mpss;      /* trajectory accel */
} TestScenario;

static const TestScenario g_scenarios[] = {
    {
        .name = "Heavy rail cart (300 lb, 50m track)",
        .params = {
            .mass_kg          = 136.0,
            .motor_Km         = 20000.0,
            .motor_Kb         = 0.03,
            .friction_static  = 35.0,
            .friction_coulomb = 20.0,
            .friction_viscous = 15.0,
            .driver_deadband  = 3.0,
            .driver_asymmetry = 0.05,
            .counts_per_meter = 10000.0,
            .encoder_noise_counts = 2.0,
            .track_min_m      = -100.0,
            .track_max_m      = 100.0,
            .dt_s             = 0.001,
        },
        .start_m    = 25.0,
        .move_dist_m = 15.0,
        .cruise_mps  = 4.0,
        .accel_mpss  = 0.75,
    },
    {
        .name = "Light rail cart (120 lb, 50m track)",
        .params = {
            .mass_kg          = 54.0,
            .motor_Km         = 10000.0,
            .motor_Kb         = 0.03,
            .friction_static  = 20.0,
            .friction_coulomb = 12.0,
            .friction_viscous = 10.0,
            .driver_deadband  = 3.0,
            .driver_asymmetry = 0.05,
            .counts_per_meter = 10000.0,
            .encoder_noise_counts = 2.0,
            .track_min_m      = -100.0,
            .track_max_m      = 100.0,
            .dt_s             = 0.001,
        },
        .start_m    = 25.0,
        .move_dist_m = 15.0,
        .cruise_mps  = 4.0,
        .accel_mpss  = 1.5,
    },
    {
        .name = "Small linear actuator (5 kg, 1m stroke)",
        .params = {
            .mass_kg          = 5.0,
            .motor_Km         = 1000.0,
            .motor_Kb         = 0.10,
            .friction_static  = 5.0,
            .friction_coulomb = 3.0,
            .friction_viscous = 2.0,
            .driver_deadband  = 2.0,
            .driver_asymmetry = 0.02,
            .counts_per_meter = 50000.0,
            .encoder_noise_counts = 1.0,
            .track_min_m      = -0.1,
            .track_max_m      = 1.1,
            .dt_s             = 0.001,
        },
        .start_m    = 0.5,
        .move_dist_m = 0.4,
        .cruise_mps  = 0.5,
        .accel_mpss  = 2.0,
    },
    {
        .name = "Heavy cart, worn bearings (300 lb, high friction)",
        .params = {
            .mass_kg          = 136.0,
            .motor_Km         = 20000.0,
            .motor_Kb         = 0.03,
            .friction_static  = 80.0,
            .friction_coulomb = 50.0,
            .friction_viscous = 25.0,
            .driver_deadband  = 5.0,
            .driver_asymmetry = 0.10,
            .counts_per_meter = 10000.0,
            .encoder_noise_counts = 4.0,
            .track_min_m      = -100.0,
            .track_max_m      = 100.0,
            .dt_s             = 0.001,
        },
        .start_m    = 25.0,
        .move_dist_m = 10.0,
        .cruise_mps  = 3.0,
        .accel_mpss  = 0.5,
    },
    {
        .name = "Rotary platform (20 kg, 2m circumference)",
        .params = {
            .mass_kg          = 20.0,
            .motor_Km         = 20000.0,
            .motor_Kb         = 0.08,
            .friction_static  = 10.0,
            .friction_coulomb = 6.0,
            .friction_viscous = 8.0,
            .driver_deadband  = 2.0,
            .driver_asymmetry = 0.03,
            .counts_per_meter = 20000.0,
            .encoder_noise_counts = 1.0,
            .track_min_m      = -100.0,
            .track_max_m      =  100.0,
            .dt_s             = 0.001,
        },
        .start_m    = 0.0,
        .move_dist_m = 1.5,
        .cruise_mps  = 1.0,
        .accel_mpss  = 3.0,
    }
};

#define NUM_SCENARIOS (sizeof(g_scenarios) / sizeof(g_scenarios[0]))
