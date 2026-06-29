# SyntropicOS — Arduino Uno Example

A complete example demonstrating SyntropicOS on the **Arduino Uno** (ATmega328P).  
Connect via USB serial at **115200 baud** to interact with the CLI.

## Features Demonstrated

| Module | What it shows |
|--------|--------------|
| **Scheduler** | Three cooperative protothread tasks at different priorities |
| **CLI** | Interactive command-line interface over serial |
| **LED** | Blink, flash, SOS Morse pattern via the `syn_led` driver |
| **ADC** | Two-channel analog input with oversampling and EMA filtering |
| **Signal** | Running min/max/mean/variance statistics on ADC samples |
| **FSM** | Three-state machine (IDLE → RUNNING → FAULT) |
| **Logging** | Structured `SYN_LOG` output with severity levels |

## Hardware Setup

```
Arduino Uno
├── Pin 13 ── On-board LED (heartbeat)
├── A0 ────── Potentiometer or sensor (optional)
└── A1 ────── Potentiometer or sensor (optional)
```

The ADC commands work without external hardware — the pins will read floating values.  
For meaningful results, connect potentiometers (wiper to A0/A1, ends to GND and 5V).

## Build & Flash

```bash
pio run -d examples/arduino_serial            # Build
pio run -d examples/arduino_serial -t upload  # Flash
pio device monitor -b 115200                  # Open serial monitor
```

## CLI Commands

```
> help              List available commands
> led on            Turn LED on
> led off           Turn LED off
> led blink         Fast blink (200ms)
> led sos           Morse code SOS pattern
> adc 0             Read A0 with filter stats
> adc 1             Read A1 with filter stats
> fsm start         Transition FSM: IDLE → RUNNING
> fsm fault         Transition FSM: RUNNING → FAULT
> fsm stop          Transition FSM: → IDLE
> tasks             Show scheduler task list
```

## Code Structure

The example is a single file ([`src/main.cpp`](src/main.cpp)) organized into sections:

1. **Application objects** — CLI, scheduler, tasks, LED
2. **ADC + Filtering** — Two channels with EMA filters and statistics windows
3. **FSM** — Transition table and state callbacks
4. **CLI commands** — `led`, `adc`, `fsm` handlers
5. **Tasks** — `blink_task` (heartbeat), `cli_task` (serial I/O), `adc_task` (sampling)
6. **`setup()` / `loop()`** — Arduino entry points

## Task Priorities

| Task | Priority | Rate | Purpose |
|------|----------|------|---------|
| `blink` | 2 (highest) | 50 ms | Keep LED animations smooth |
| `cli` | 1 | every cycle | Responsive serial input |
| `adc` | 0 (lowest) | 200 ms | Background sensor sampling |

Higher-priority tasks are checked first each scheduler cycle.
