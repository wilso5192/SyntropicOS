#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#include "syntropic/motor/syn_motor_ctrl.h"
#include "syntropic/motor/syn_dc_motor.h"
#include "syntropic/control/syn_autotune.h"
#include "syntropic/control/syn_pid.h"
#include "syntropic/control/syn_control_stats.h"
#include "sim_plant.h"

#define UPDATE_HZ 1000
#define SIM_STEP_MS (1000 / UPDATE_HZ)

/* ── Mock/Sim Helpers ────────────────────────────────────────────────────── */

static SimPlant      g_plant;
static SYN_MotorCtrl g_ctrl;
static SYN_DCMotor   g_dc_motor;
static uint32_t      g_sim_tick_ms = 0;

static int32_t read_encoder(void *ctx) {
    (void)ctx;
    return g_plant.encoder_counts;
}

uint32_t syn_port_get_tick_ms(void) {
    return g_sim_tick_ms;
}

static void step_sim(void) {
    sim_plant_step(&g_plant);
    g_sim_tick_ms += SIM_STEP_MS;
}

/* Mock DC motor output */
static void mock_set_output(void *ctx, int32_t output) {
    (void)ctx;
    sim_plant_set_command(&g_plant, (double)output);
}

static void mock_coast(void *ctx) {
    (void)ctx;
    sim_plant_set_command(&g_plant, 0.0);
}

SYN_MotorOutput syn_dc_motor_output(SYN_DCMotor *motor) {
    (void)motor;
    SYN_MotorOutput out = {
        .ctx = NULL,
        .set_output = mock_set_output,
        .coast = mock_coast,
        .brake = mock_coast
    };
    return out;
}

void syn_dc_motor_init(SYN_DCMotor *motor, SYN_GPIO_Pin pin_a, SYN_GPIO_Pin pin_b, SYN_DCMotorMode mode) {
    (void)motor; (void)pin_a; (void)pin_b; (void)mode;
}

/* System Mocks */
void syn_assert_failed(const char *file, uint32_t line, const char *expr) {
    fprintf(stderr, "ASSERT FAILED: %s:%u (%s)\n", file, line, expr);
    exit(1);
}

void syn_errlog_record(SYN_ErrLog *log, uint16_t code, SYN_ErrSeverity severity, uint32_t context) {
    (void)log;
    fprintf(stderr, "ERRLOG: code=%u sev=%d ctx=%u\n", code, (int)severity, context);
}

/* ── Globals ────────── */

typedef struct {
    const char     *name;
    SimPlantParams  params;
    double          start_m;
} TestScenario;

static const TestScenario g_scenarios[] = {
    {
        .name = "Heavy rail cart (300 lb, 50m track)",
        .params = {
            .mass_kg          = 136.0, .motor_Km = 600.0, .motor_Kb = 0.005,
            .friction_static  = 40.0, .friction_coulomb = 25.0, .friction_viscous = 1.0,
            .driver_deadband  = 3.0, .driver_asymmetry = 0.05, .counts_per_meter = 10000.0,
            .track_min_m      = -100.0, .track_max_m = 1000.0, .dt_s = 0.001,
        },
        .start_m = 25.0,
    },
    {
        .name = "Light rail cart (120 lb, 50m track)",
        .params = {
            .mass_kg          = 54.0, .motor_Km = 300.0, .motor_Kb = 0.04,
            .friction_static  = 20.0, .friction_coulomb = 12.0, .friction_viscous = 0.5,
            .driver_deadband  = 3.0, .driver_asymmetry = 0.05, .counts_per_meter = 10000.0,
            .track_min_m      = -100.0, .track_max_m = 1000.0, .dt_s = 0.001,
        },
        .start_m = 25.0,
    },
    {
        .name = "Small linear actuator (5 kg, 1m stroke)",
        .params = {
            .mass_kg          = 5.0, .motor_Km = 50.0, .motor_Kb = 0.10,
            .friction_static  = 5.0, .friction_coulomb = 3.0, .friction_viscous = 0.5,
            .driver_deadband  = 2.0, .driver_asymmetry = 0.02, .counts_per_meter = 50000.0,
            .track_min_m      = -1.0, .track_max_m = 10.0, .dt_s = 0.001,
        },
        .start_m = 0.5,
    },
    {
        .name = "Heavy cart, worn bearings (300 lb, high friction)",
        .params = {
            .mass_kg          = 136.0, .motor_Km = 600.0, .motor_Kb = 0.005,
            .friction_static  = 100.0, .friction_coulomb = 60.0, .friction_viscous = 0.5,
            .driver_deadband  = 5.0, .driver_asymmetry = 0.10, .counts_per_meter = 10000.0,
            .track_min_m      = -100.0, .track_max_m = 1000.0, .dt_s = 0.001,
        },
        .start_m = 25.0,
    },
    {
        .name = "Rotary platform (20 kg, 2m circumference)",
        .params = {
            .mass_kg          = 20.0, .motor_Km = 150.0, .motor_Kb = 0.08,
            .friction_static  = 10.0, .friction_coulomb = 6.0, .friction_viscous = 0.5,
            .driver_deadband  = 2.0, .driver_asymmetry = 0.03, .counts_per_meter = 20000.0,
            .track_min_m      = -100.0, .track_max_m = 100.0, .dt_s = 0.001,
        },
        .start_m = 0.0,
    },
    {
        .name = "KA Ident Test (Low mass, high torque)",
        .params = {
            .mass_kg = 2.0, .motor_Km = 1000.0, .motor_Kb = 0.001,
            .friction_static = 0.5, .friction_coulomb = 0.2, .friction_viscous = 0.05,
            .driver_deadband = 1.0, .counts_per_meter = 10000.0,
            .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001,
        },
        .start_m = 0.0,
    },
    {
        .name = "ZN Classic Method Test",
        .params = { .mass_kg = 2.0, .motor_Km = 100.0, .counts_per_meter = 10000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
    },
    {
        .name = "ZN No Overshoot Method Test",
        .params = { .mass_kg = 2.0, .motor_Km = 100.0, .counts_per_meter = 10000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
    },
    {
        .name = "Abort: Position Limit",
        .params = { .mass_kg = 10.0, .motor_Km = 100.0, .counts_per_meter = 10000.0, .track_min_m = -1.0, .track_max_m = 1.0, .dt_s = 0.001 },
        .start_m = 0.5,
    },
    {
        .name = "Abort: Velocity Limit",
        .params = { .mass_kg = 1.0, .motor_Km = 10000.0, .counts_per_meter = 1000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
    },
    {
        .name = "Abort: No Motion",
        .params = { .mass_kg = 1000.0, .motor_Km = 1.0, .friction_static = 100.0, .counts_per_meter = 1000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
    },
    {
        .name = "Abort: Watchdog",
        .params = { .mass_kg = 10.0, .motor_Km = 100.0, .counts_per_meter = 10000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
    },
    {
        .name = "Abort: Soft Limit",
        .params = { .mass_kg = 10.0, .motor_Km = 100.0, .counts_per_meter = 10000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
        .start_m = 0.5,
    },
    {
        .name = "Scale Increment Test (High Amplitude)",
        .params = { .mass_kg = 1.0, .motor_Km = 200000.0, .friction_static = 0.1, .counts_per_meter = 1000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
    },
    {
        .name = "Datalogging Test",
        .params = { .mass_kg = 10.0, .motor_Km = 100.0, .counts_per_meter = 10000.0, .track_min_m = -100.0, .track_max_m = 100.0, .dt_s = 0.001 },
    }
};

#define NUM_SCENARIOS (int)(sizeof(g_scenarios)/sizeof(g_scenarios[0]))

typedef struct {
    int32_t kp, ki, kd;
    uint8_t pid_scale;
    int32_t ff_kv, ff_ka;
    uint8_t ff_scale;
    int at_state;
    int at_reason;
} TestResult;

static void init_plant_and_ctrl(int index) {
    const TestScenario *sc = &g_scenarios[index];
    sim_plant_init(&g_plant, &sc->params);
    g_plant.position_m = sc->start_m;
    g_plant.encoder_counts = (int32_t)(sc->start_m * sc->params.counts_per_meter);
    g_sim_tick_ms = 0;

    SYN_MotorCtrl_Config cfg = {
        .motor        = syn_dc_motor_output(&g_dc_motor),
        .read_pos     = read_encoder,
        .pid_scale    = (index == 13) ? 1 : 16,
        .ff_scale     = 16,
        .update_hz    = UPDATE_HZ,
        .output_min   = -100,
        .output_max   = 100,
        .position_min = -2000000, /* Effectively disabled */
        .position_max = 2000000,  /* Effectively disabled */
        .position_deadband = 5
    };
    syn_motor_ctrl_init(&g_ctrl, &cfg);
}

static void run_full_autotune(int index, TestResult *res) {
    const TestScenario *sc = &g_scenarios[index];
    SYN_AutoTune at;
    
    /* Coverage: Trigger NULL guards */
    syn_autotune_abort(NULL);
    
    SYN_AutoTune_Limits limits = {
        .position_min = (index == 8) ? (int32_t)(sc->start_m * sc->params.counts_per_meter) : (int32_t)(sc->params.track_min_m * sc->params.counts_per_meter),
        .position_max = (index == 8) ? (int32_t)(sc->start_m * sc->params.counts_per_meter + 10) : (int32_t)(sc->params.track_max_m * sc->params.counts_per_meter),
        .max_velocity = (index == 9) ? 1 : 1000000, 
        .watchdog_ms = 60000
    };

    SYN_AutoTune_Method method = SYN_ATUNE_TYREUS_LUYBEN;
    if (index == 6) method = SYN_ATUNE_ZN_CLASSIC;
    if (index == 7) method = SYN_ATUNE_ZN_NO_OVERSHOOT;

    /* Coverage: Trigger Watchdog for index 11 */
    if (index == 11) limits.watchdog_ms = 1;
    
    /* Coverage: Trigger Soft Limit for index 12 */
    if (index == 12) {
        g_ctrl.cfg.position_min = (int32_t)(sc->start_m * sc->params.counts_per_meter) - 1;
        g_ctrl.cfg.position_max = (int32_t)(sc->start_m * sc->params.counts_per_meter) + 1;
    }

    static SYN_DataLog g_datalog;
    static uint8_t g_datalog_buf[4096];
    syn_datalog_init(&g_datalog, g_datalog_buf, sizeof(g_datalog_buf));

    if (index == 5) {
        syn_autotune_start(&at, &g_ctrl, &limits, SYN_ATUNE_FLAG_ALL);
    } else {
        SYN_AutoTune_Flags flags = SYN_ATUNE_FLAG_ALL;
        if (index == 13) flags = SYN_ATUNE_FLAG_IDENT_KV; /* Skip PID to hit line 395 */

        SYN_AutoTune_Config cfg = {
            .mode           = (index >= 6 && index <= 7) ? SYN_ATUNE_MODE_RELAY : SYN_ATUNE_MODE_AUTO,
            .flags          = flags,
            .test_output    = (index == 8) ? 20 : ((index >= 6 && index <= 7) ? 40 : ((index == 13 || index == 5) ? 50 : 0)),
            .setpoint       = (index == 6) ? 1000 : 0, /* Force above/below setpoint variations */
            .limits         = limits,
            .relay_cycles   = (index >= 6 && index <= 7) ? 8 : 2,
            .method         = method,
            .ramp_ms        = 100,
            .watchdog_ms    = limits.watchdog_ms,
            .settle_ms      = 200,
            .measure_ms     = 200,
            .datalog        = (index == 14) ? &g_datalog : NULL
        };
        syn_autotune_init(&at, &g_ctrl, &cfg);
    }

    int steps = 0;
    int max_steps = 120 * UPDATE_HZ;
    for (; steps < max_steps && at.state != SYN_ATUNE_DONE && at.state != SYN_ATUNE_ABORTED; steps++) {
        /* Coverage: Trigger User Abort in first scenario */
        if (index == 0 && steps == 50) {
            syn_autotune_abort(&at);
        }
        
        step_sim();
        g_sim_tick_ms += 1; /* Advance 1ms per step at 1kHz */
        g_ctrl.measured_position = g_plant.encoder_counts;
        g_ctrl.measured_velocity = (int32_t)(g_plant.velocity_mps * sc->params.counts_per_meter);
        
        /* Coverage: Force watchdog timeout */
        if (index == 11 && steps == 10) {
            g_sim_tick_ms += 1000; /* Advance clock by 1s */
        }

        /* Coverage: Force default case in switch(at->state) */
        if (index == 14 && steps == 5) {
            at.state = (SYN_AutoTune_State)99;
            syn_autotune_update(&at);
        }

        syn_autotune_update(&at);
        
        if (g_plant.crashed) break;
    }

    /* Coverage: Test syn_autotune_apply if done */
    if (at.state == SYN_ATUNE_DONE) {
        fprintf(stderr, "      [AT] Calling syn_autotune_apply for scenario %d\n", index);
        syn_autotune_apply(&at);
    }

    /* Coverage: API accessors */
    const SYN_AutoTune_Result *r = syn_autotune_result(&at);
    (void)r;
    (void)syn_autotune_abort_reason(&at);

    res->at_state = at.state;
    res->at_reason = at.abort_reason;
    if (at.state == SYN_ATUNE_DONE) {
        res->kp = r->kp;
        res->ki = r->ki;
        res->kd = r->kd;
        res->ff_kv = r->ff_kv;
        res->ff_ka = r->ff_ka;
        res->ff_scale = r->ff_scale;
    }
}

int main(void) {
    fprintf(stderr, "SyntropicOS Coverage Suite\n");
    for (int i = 0; i < NUM_SCENARIOS; i++) {
        TestResult res = {0};
        init_plant_and_ctrl(i);
        run_full_autotune(i, &res);
        fprintf(stderr, "Scenario %d (%s): state=%d, reason=%d, gains=(%d,%d,%d), ff=(%d,%d)\n", 
                i, g_scenarios[i].name, res.at_state, res.at_reason,
                res.kp, res.ki, res.kd, res.ff_kv, res.ff_ka);
    }
    return 0;
}
