/**
 * @file    main.cpp
 * @brief   SyntropicOS Example — Arduino Uno (ATmega328P)
 *
 * Demonstrates SyntropicOS features on the Arduino Uno:
 *
 *   - Cooperative scheduler with protothread-based tasks
 *   - Serial CLI with custom commands (115200 baud)
 *   - Structured logging (SYN_LOG)
 *   - LED driver with blink, flash, and Morse pattern support (pin 13)
 *   - ADC driver with EMA filtering and running statistics (A0, A1)
 *   - Finite State Machine (FSM) module
 *   - Digital signal processing: EMA filter + signal statistics
 *
 * Hardware:
 *   - Board: Arduino Uno (ATmega328P, 16 MHz, 32 KB Flash, 2 KB RAM)
 *   - LED:   Pin 13 (active-high, on-board)
 *   - UART:  USB serial at 115200 8N1
 *   - ADC:   A0 and A1 (connect potentiometers or sensors)
 *
 * To build:
 *   pio run -d examples/arduino_serial
 */

#include <Arduino.h>
#include "syntropic/syntropic.h"
#include "syntropic/port/syn_port_uart.h"
#include "syntropic/port/syn_port_system.h"
#include "syntropic/sched/syn_timer.h"
#include "syntropic/control/syn_pid.h"
#include "syntropic/dsp/syn_filter.h"
#include "syntropic/dsp/syn_signal.h"
#include "syntropic/util/syn_fsm.h"
#include "syntropic/drivers/syn_adc.h"
#include "syntropic/output/syn_led.h"
#include <string.h>
#include <stdlib.h>

#define TAG     "main"
#define LED_PIN 13

/* ── Application Objects ────────────────────────────────────────────────── */

static SYN_CLI   cli;
static SYN_Sched sched;
static SYN_Task  tasks[3];
static SYN_LED   status_led;

/* ── ADC + Filtering + Statistics ───────────────────────────────────────── */
/*
 * Two analog channels (A0 and A1) are sampled in the background by a
 * dedicated task.  Each channel has:
 *   - 4× oversampling for noise reduction
 *   - An exponential moving average (EMA) filter (alpha = 0.25)
 *   - A running statistics window (min, max, mean, variance)
 *
 * Use the `adc 0` or `adc 1` CLI command to inspect live values.
 */

static SYN_ADC       adc_a0, adc_a1;
static SYN_FilterEMA ema_a0, ema_a1;
static int32_t       stats_buf_a0[8], stats_buf_a1[8];
static SYN_Signal    signal_a0, signal_a1;

/* Latest readings, updated by the ADC task */
static int16_t raw_a0, filtered_a0;
static int16_t raw_a1, filtered_a1;

/* ── Finite State Machine ───────────────────────────────────────────────── */
/*
 * A simple three-state FSM demonstrating SyntropicOS state management.
 *
 *   IDLE --[start]--> RUNNING --[stop]--> IDLE
 *                     RUNNING --[fault]--> FAULT --[stop]--> IDLE
 */

static SYN_FSM my_fsm;
static bool    fsm_initialized = false;

static const char *state_names[] = { "IDLE", "RUNNING", "FAULT" };

static void on_enter_running(void *ctx) { (void)ctx; SYN_LOG_I(TAG, "FSM -> RUNNING"); }
static void on_enter_fault(void *ctx)   { (void)ctx; SYN_LOG_W(TAG, "FSM -> FAULT"); }
static void on_enter_idle(void *ctx)    { (void)ctx; SYN_LOG_I(TAG, "FSM -> IDLE"); }

static const SYN_FSM_Transition transitions[] = {
    { 0, 0, 1, NULL, on_enter_running },   /* IDLE    --start--> RUNNING */
    { 1, 1, 0, NULL, on_enter_idle },      /* RUNNING --stop-->  IDLE    */
    { 1, 2, 2, NULL, on_enter_fault },     /* RUNNING --fault--> FAULT   */
    { 2, 1, 0, NULL, on_enter_idle },      /* FAULT   --stop-->  IDLE    */
    SYN_FSM_END
};

/* ── CLI Commands ───────────────────────────────────────────────────────── */

/** CLI command: read ADC channel — `adc <0|1>` */
static int cmd_adc(int argc, char *argv[])
{
    if (argc < 2) {
        syn_cli_printf(&cli, "Usage: adc <0|1>\r\n");
        return 1;
    }

    int ch = atoi(argv[1]);
    SYN_Signal *sig  = (ch == 0) ? &signal_a0 : &signal_a1;
    int16_t     raw  = (ch == 0) ? raw_a0     : raw_a1;
    int16_t     filt = (ch == 0) ? filtered_a0 : filtered_a1;

    if (ch != 0 && ch != 1) {
        syn_cli_printf(&cli, "Invalid channel: %d\r\n", ch);
        return 1;
    }

    int32_t var_q16  = syn_signal_variance_q16(sig);
    int32_t var_int  = var_q16 >> 16;
    int32_t var_frac = ((var_q16 & 0xFFFF) * 1000) >> 16;

    syn_cli_printf(&cli,
        "A%d: samples=%d raw=%d filtered=%d "
        "min=%ld max=%ld mean=%ld var=%ld.%03ld\r\n",
        ch, (int)syn_signal_count(sig), raw, filt,
        syn_signal_min(sig), syn_signal_max(sig),
        syn_signal_mean(sig), var_int, var_frac);
    return 0;
}

/** CLI command: control the FSM — `fsm <start|stop|fault>` */
static int cmd_fsm(int argc, char *argv[])
{
    if (argc < 2) {
        syn_cli_printf(&cli, "Usage: fsm <start|stop|fault>\r\n");
        return 1;
    }

    if (!fsm_initialized) {
        syn_fsm_init(&my_fsm, transitions, 0, "demo_fsm");
        syn_fsm_set_state_names(&my_fsm, state_names);
        fsm_initialized = true;
    }

    SYN_FSM_Event event = -1;
    if      (strcmp(argv[1], "start") == 0) event = 0;
    else if (strcmp(argv[1], "stop")  == 0) event = 1;
    else if (strcmp(argv[1], "fault") == 0) event = 2;
    else {
        syn_cli_printf(&cli, "Unknown event: %s\r\n", argv[1]);
        return 1;
    }

    bool ok = syn_fsm_dispatch(&my_fsm, event);
    syn_cli_printf(&cli, "FSM: event=%s result=%s state=%s\r\n",
                   argv[1], ok ? "OK" : "IGNORED",
                   state_names[syn_fsm_state(&my_fsm)]);
    return 0;
}

/** CLI command: control the LED — `led <on|off|blink|flash|sos>` */
static int cmd_led(int argc, char *argv[])
{
    if (argc < 2) {
        syn_cli_printf(&cli, "Usage: led <on|off|blink|flash|sos>\r\n");
        return 1;
    }

    if      (strcmp(argv[1], "on")    == 0) { syn_led_on(&status_led);                            }
    else if (strcmp(argv[1], "off")   == 0) { syn_led_off(&status_led);                           }
    else if (strcmp(argv[1], "blink") == 0) { syn_led_blink(&status_led, 200, 200);               }
    else if (strcmp(argv[1], "flash") == 0) { syn_led_flash(&status_led, 100, 100, 5);            }
    else if (strcmp(argv[1], "sos")   == 0) { syn_led_pattern(&status_led, "... --- ... |", 150); }
    else {
        syn_cli_printf(&cli, "Unknown mode: %s\r\n", argv[1]);
        return 1;
    }

    syn_cli_printf(&cli, "LED: %s\r\n", argv[1]);
    return 0;
}

/** Command table registered with the CLI */
static const SYN_CLI_Command commands[] = {
    { "led", "Control the status LED",            cmd_led },
    { "adc", "Read ADC channel with statistics",  cmd_adc },
    { "fsm", "Drive the demo state machine",      cmd_fsm },
};

/* ── Callbacks ──────────────────────────────────────────────────────────── */

static void cli_putchar(char ch)
{
    syn_port_uart_transmit_byte(0, (uint8_t)ch);
}

static void log_output(const char *str, size_t len)
{
    syn_port_uart_transmit(0, (const uint8_t *)str, len, 100);
}

extern "C" void syn_assert_failed(const char *file, int line)
{
    (void)file; (void)line;
    for (;;);
}

/* ── Tasks ──────────────────────────────────────────────────────────────── */

/**
 * Task 1: Heartbeat — updates the LED pattern every 50 ms.
 */
static SYN_PT_Status blink_task(SYN_PT *pt, SYN_Task *task)
{
    PT_BEGIN(pt);
    for (;;) {
        syn_led_update(&status_led);
        PT_TASK_DELAY_MS(pt, task, 50);
    }
    PT_END(pt);
}

/**
 * Task 2: Serial CLI — polls the UART for incoming characters.
 */
static SYN_PT_Status cli_task(SYN_PT *pt, SYN_Task *task)
{
    uint8_t    ch;
    size_t     received;
    SYN_Status status;
    (void)task;

    PT_BEGIN(pt);
    for (;;) {
        received = 0;
        status = syn_port_uart_receive(0, &ch, 1, &received, 1);
        if (status == SYN_OK && received > 0) {
            syn_cli_process_char(&cli, (char)ch);
        }
        PT_YIELD(pt);
    }
    PT_END(pt);
}

/**
 * Task 3: ADC sampling — reads A0 and A1 every 200 ms.
 *
 * Demonstrates cooperative multitasking: the task yields between the
 * two channel reads, giving other tasks a chance to run.
 */
static SYN_PT_Status adc_task(SYN_PT *pt, SYN_Task *task)
{
    PT_BEGIN(pt);
    for (;;) {
        /* Sample channel 0 */
        syn_adc_read(&adc_a0);
        raw_a0      = (int16_t)syn_adc_raw(&adc_a0);
        filtered_a0 = (int16_t)syn_adc_filtered(&adc_a0);

        PT_YIELD(pt);   /* Cooperative yield between channels */

        /* Sample channel 1 */
        syn_adc_read(&adc_a1);
        raw_a1      = (int16_t)syn_adc_raw(&adc_a1);
        filtered_a1 = (int16_t)syn_adc_filtered(&adc_a1);

        PT_TASK_DELAY_MS(pt, task, 200);
    }
    PT_END(pt);
}

/* ── Arduino Entry Points ───────────────────────────────────────────────── */

void setup()
{
    /* 1. Initialize the UART port layer */
    syn_port_uart_init(0, 115200);

    /* 2. Initialize the on-board LED (pin 13, active-high) */
    syn_led_init(&status_led, LED_PIN, SYN_LED_ACTIVE_HIGH);
    syn_led_blink(&status_led, 500, 500);

    /* 3. Initialize signal processing and ADC */
    syn_signal_init(&signal_a0, stats_buf_a0, 8);
    syn_signal_init(&signal_a1, stats_buf_a1, 8);
    syn_filter_ema_init(&ema_a0, 64);   /* alpha ≈ 0.25 */
    syn_filter_ema_init(&ema_a1, 64);

    SYN_ADC_Config cfg_a0 = {
        .channel         = 0,
        .oversample      = 4,
        .filter          = &ema_a0,
        .filter_type     = SYN_ADC_FILTER_EMA,
        .cal_offset      = 0,
        .cal_scale        = 1,
        .cal_scale_shift = 0
    };
    syn_adc_init(&adc_a0, &cfg_a0);
    syn_adc_set_stats(&adc_a0, &signal_a0);

    SYN_ADC_Config cfg_a1 = {
        .channel         = 1,
        .oversample      = 4,
        .filter          = &ema_a1,
        .filter_type     = SYN_ADC_FILTER_EMA,
        .cal_offset      = 0,
        .cal_scale        = 1,
        .cal_scale_shift = 0
    };
    syn_adc_init(&adc_a1, &cfg_a1);
    syn_adc_set_stats(&adc_a1, &signal_a1);

    /* 4. Initialize structured logging */
    syn_log_init(log_output, SYN_LOG_INFO);
    SYN_LOG_I(TAG, "boot");

    /* 5. Initialize the interactive CLI */
    syn_cli_init(&cli, commands,
                 sizeof(commands) / sizeof(commands[0]),
                 cli_putchar, "> ");
    syn_cli_set_scheduler(&sched);
    syn_cli_printf(&cli, "\r\n--- SyntropicOS ---\r\n");
    syn_cli_print_prompt(&cli);

    /* 6. Create tasks and start the scheduler
     *    Priority: blink(2) > cli(1) > adc(0)
     *    Higher-priority tasks are checked first each cycle.
     */
    syn_task_create(&tasks[0], "blink", blink_task, 2, NULL);
    syn_task_create(&tasks[1], "cli",   cli_task,   1, NULL);
    syn_task_create(&tasks[2], "adc",   adc_task,   0, NULL);
    syn_sched_init(&sched, tasks, 3);
}

void loop()
{
    syn_sched_run(&sched);
}
