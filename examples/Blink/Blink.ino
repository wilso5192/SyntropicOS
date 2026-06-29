/**
 * SyntropicOS — Blink
 *
 * The simplest SyntropicOS program: one task, one LED.
 * The on-board LED blinks using the cooperative scheduler
 * and the LED pattern driver.
 *
 * Works on: Arduino Uno, Mega, Nano, ESP32, STM32duino, RP2040, etc.
 */

#include <SyntropicOS.h>
#include <syntropic/sched/syn_sched.h>
#include <syntropic/output/syn_led.h>

static SYN_Sched sched;
static SYN_Task  tasks[1];
static SYN_LED   led;

/* Blink task — updates the LED pattern every 50ms */
static SYN_PT_Status blink_task(SYN_PT *pt, SYN_Task *task)
{
    PT_BEGIN(pt);
    for (;;) {
        syn_led_update(&led);
        PT_TASK_DELAY_MS(pt, task, 50);
    }
    PT_END(pt);
}

void setup()
{
    syn_led_init(&led, LED_BUILTIN, SYN_LED_ACTIVE_HIGH);
    syn_led_blink(&led, 500, 500);

    syn_task_create(&tasks[0], "blink", blink_task, 0, NULL);
    syn_sched_init(&sched, tasks, 1);
}

void loop()
{
    syn_sched_run(&sched);
}
