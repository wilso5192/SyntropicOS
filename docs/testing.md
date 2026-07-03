# Testing

SyntropicOS has two test tiers to balance iteration speed with hardware-level fidelity.

## Tier 1: Host-Side Unity Tests (Fast Iteration)

A comprehensive host-compiled test suite using the **Unity** test framework. Tests logic, scheduler, data structures, DSP modules, and more using a mock port layer. Compiles and runs in under a second on your host PC.

```bash
make -f tests/Makefile.unity test
```

## Tier 2: Target-Side Renode Simulation (Hardware-Level Fidelity)

Verifies real peripheral drivers (SysTick, GPIO, UART, EXTI) and networking protocols on a simulated **STM32F4 Discovery** board using **Renode** and **Robot Framework**.

**Prerequisites:** `arm-none-eabi-gcc` and Renode installed.

### Single-Node Peripheral Verification

Validates SysTick interrupts, GPIO outputs/inputs, COBS, ring buffer, and CRC logic directly on the emulated Cortex-M4 target:

```bash
make -f port/stm32f4/Makefile.renode renode-test
```

### Multi-Node Network Stack Verification

Spawns two simulated STM32F4 nodes connected via a virtual UART hub, checking node-to-node routing, COBS transport framing, and heartbeat discovery end-to-end:

```bash
make -f port/stm32f4/Makefile.renode renode-multinode-test
```

!!! tip
    Both Renode test targets run with `--show-log` by default, streaming full UART debug messages and emulator events to the console in real time. If a test fails, you'll immediately see exactly what each simulated node output before the error. To override flags: `make -f port/stm32f4/Makefile.renode renode-test RENODE_FLAGS=""`.

## Tier 3: Control Simulation (Plant Harness)

For verifying motor control and autotune logic, SyntropicOS includes a high-fidelity **physics simulation harness**. This simulator models mass, friction (static, coulomb, viscous), back-EMF, and encoder noise.

It is used to verify that the `syn_autotune` module can successfully identify and control diverse plant configurations (from 300lb carts to small linear actuators) at **1kHz**.

```bash
# Run the control simulation harness
gcc -O2 -Isrc -o sim_harness tests/sim/sim_harness.c tests/sim/sim_plant.c \
    src/syntropic/motor/syn_motor_ctrl.c \
    src/syntropic/control/syn_autotune.c \
    src/syntropic/control/syn_pid.c \
    -lm
./sim_harness
```

The harness tests the entire pipeline: **Automated Tuning** $\to$ **Profile Generation** $\to$ **Trajectory Tracking**.

## Visual Validation & Simulation (GUI)

SyntropicOS provides host-compiled tools to validate canvas drawing and IMGUI layouts without hardware.

### Static Mockup Capture

Renders a 16bpp (RGB565) dashboard mockup to a raw framebuffer file (`tests/framebuffer.bin`):

```bash
gcc -std=c99 -pedantic -Wall -Wextra -Werror -I. -Itests \
    tests/gen_screenshot.c src/syntropic/display/syn_canvas.c \
    src/syntropic/ui/syn_imgui.c tests/mocks/mock_port.c \
    -o gen_screenshot
./gen_screenshot
```

The output is a raw 128×64 RGB565 framebuffer (16,384 bytes). Convert to PNG with any raw-to-image tool or a short Python script using Pillow.

### Animated Live Simulation

Renders a 100-frame interactive GUI timeline (encoder ticks, button presses, touch drags, modal dialogs) to a series of raw framebuffer files:

```bash
gcc -std=c99 -pedantic -Wall -Wextra -Werror -I. -Itests \
    tests/gen_gif.c src/syntropic/display/syn_canvas.c \
    src/syntropic/ui/syn_imgui.c tests/mocks/mock_port.c \
    -o gen_gif
./gen_gif
```

## Advanced Diagnostics & Verification

SyntropicOS supports several advanced compiler-level verification tools to guarantee code quality, memory safety, and portability.

### 1. Code Coverage (Line & Branch Coverage)

You can clean, rebuild, run the tests, and generate both console summaries and visual HTML reports with full branch coverage enabled:

```bash
make -f tests/Makefile.unity clean coverage
```

The output report will detail line, function, and branch coverage. The HTML report will be generated under `coverage_html/index.html`.

### 2. Runtime Sanitizers (ASan & UBSan)

AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) compile instrumentation checks into the binary to catch memory corruption, overflows, null pointer dereferences, and signed integer shifts at runtime:

```bash
make -f tests/Makefile.unity clean test CFLAGS="-std=gnu99 -g -fsanitize=address,undefined -I. -Isrc -Itests -Itests/mocks"
```

Any runtime violation will cause the test suite to abort and print a detailed stack trace of the violation.

### 3. Protocol Fuzz Testing

The communication protocols (COBS and Modbus) have LLVM `libFuzzer` targets compiled via Clang:

```bash
# Fuzz COBS decoder
make -f tests/Makefile.fuzz fuzz-cobs

# Fuzz Modbus frame parser
make -f tests/Makefile.fuzz fuzz-modbus
```

This mutates packet data randomly at high speed to find inputs that cause memory corruptions or decoder hangs.

### 4. Stack Usage Analysis

Since stack overflow is a primary cause of micro-controller crashes, GCC can statically calculate the stack usage of every function:

```bash
# Compile and generate .su files
make -f tests/Makefile.unity clean test CFLAGS="-std=gnu99 -fstack-usage -I. -Isrc -Itests -Itests/mocks"
```

This generates a `.su` text file for each compiled source file. You can find the heaviest stack frame allocations in production by sorting the outputs:

```bash
find . -name "*.su" -exec cat {} + | grep "src/syntropic/" | sort -n -k 2 -r | head -n 20
```

### 5. Static Analysis (cppcheck)

cppcheck runs over all `src/syntropic/` sources with warning, style, performance, and portability checks enabled:

```bash
make -f tests/Makefile.check check
```

For exhaustive branch analysis of specific modules:

```bash
cppcheck --check-level=exhaustive --enable=all --std=c99 --force \
    --library=tests/cppcheck_syntropic.cfg \
    --suppress=missingIncludeSystem -I src \
    src/syntropic/crypto/ src/syntropic/net/syn_wg.c
```

### 6. GCC Static Analyzer

GCC's `-fanalyzer` performs interprocedural path-sensitive analysis to detect use-after-free, double-free, null dereference, and buffer overflow:

```bash
gcc -std=c99 -Wall -Wextra -Wpedantic -Werror -fanalyzer \
    -fsyntax-only -I src src/syntropic/crypto/*.c src/syntropic/net/syn_wg.c
```

## Crypto & WireGuard Test Coverage

The cryptographic and networking stack has dedicated Unity test suites with known-answer vectors verified against Go and Python reference implementations.

| Test File | Module | Tests | Vectors/Technique |
|---|---|---|---|
| `test_crypto.c` | BLAKE2s | 12 | RFC 7693 vectors, multi-block, streaming, keyed MAC, HMAC |
| `test_crypto.c` | ChaCha20-Poly1305 | 10 | RFC 8439 vectors, tamper detection (ct/tag/AAD), round-trip |
| `test_crypto.c` | X25519 | 7 | RFC 7748 vectors, clamp, shared secret symmetry, low-order |
| `test_wg.c` | WireGuard internals | 17 | HKDF, Noise helpers, MAC1, anti-replay window, full handshake vs Go |

!!! note "Reference vectors"
    The expected intermediate values for the Noise_IKpsk2 handshake (construction hash, chaining key, encrypted static, encrypted timestamp) were generated with Go's `golang.org/x/crypto` and are embedded as hex literals in `test_wg_handshake_intermediates`. All 9 intermediate values are verified step-by-step.

### Integration Test

A standalone integration test verifies a live WireGuard handshake and ICMP ping through a real tunnel:

```bash
gcc -std=c99 -O2 -I src -o test_wg_integration \
    tests/test_wg_integration.c \
    src/syntropic/crypto/syn_blake2s.c \
    src/syntropic/crypto/syn_chacha20poly1305.c \
    src/syntropic/crypto/syn_x25519.c
./test_wg_integration
```
