# System

| Module | Header | Config | Description |
|---|---|---|---|
| Boot Manager | `system/syn_boot.h` | `SYN_USE_BOOT` | Crash-loop detection, safe mode, and boot counter. Events auto-record to errlog via `syn_boot_log_events()`. |
| Error Log | `system/syn_errlog.h` | `SYN_USE_ERRLOG` | Persistent error registry storing severity, error code, context, and timestamp for each entry |
| Version | `system/syn_version.h` | Always available | Compile-time build info: semantic version (`SYN_VERSION_MAJOR/MINOR/PATCH`), build date/time, git hash, app name. Access via `syn_version()` struct or individual defines. |
| Sleep | `system/syn_sleep.h` | Always available | Sleep coordinator with wakelock reference counting |
| Power | `system/syn_power.h` | `SYN_USE_POWER` | Power management: sleep and wake with reference counting |
| Firmware Boot | `system/syn_fwboot.h` | Always available | Firmware boot slot validation |
| Firmware Image | `system/syn_fwimage.h` | Always available | Firmware image header definitions |
| Firmware Update | `system/syn_fwupdate.h` | Always available | Streaming, transport-agnostic firmware updater. Optional HMAC-SHA256 verification via `SYN_FW_USE_HMAC`. |
| System Faults | `system/syn_fault.h` | `SYN_USE_FAULT` | Exception and hardware fault handler with diagnostic logging (e.g., HardFault on Cortex-M) |
| Core Dump | `system/syn_coredump.h` | `SYN_USE_COREDUMP` | Persistent fault dump to flash — saves registers + partial stack snapshot on crash, readable at next boot |

## Core Dump

The core dump module captures CPU register state and a partial stack snapshot during a hard fault, persisting them to a reserved flash sector. On the next boot, the dump can be read back for post-mortem diagnostics.

Requires `SYN_USE_FAULT`, CRC, and a flash port. Configure in `syn_config.h`:

```c
#define SYN_USE_COREDUMP        1
#define SYN_COREDUMP_FLASH_ADDR 0x0803F800  // Reserved flash sector
#define SYN_COREDUMP_STACK_SIZE 128         // Bytes of stack to capture
```

```c
// In your HardFault handler:
syn_coredump_save(&fault_context);

// At boot — check for previous crash:
SYN_CoreDump dump;
if (syn_coredump_read(&dump)) {
    printf("Previous crash at PC=0x%08x, uptime=%lu ms\n",
           dump.regs.pc, dump.uptime_ms);
    syn_coredump_clear();
}
```

## Version System

The version system provides both compile-time defines and a runtime struct:

```c
// Compile-time
printf("SyntropicOS v%d.%d.%d built %s\n",
       SYN_VERSION_MAJOR, SYN_VERSION_MINOR, SYN_VERSION_PATCH,
       SYN_BUILD_DATE);

// Runtime struct
const SYN_Version *v = syn_version();
printf("%s v%d.%d.%d [%s]\n",
       v->app_name, v->major, v->minor, v->patch, v->git_hash);
```

Override in your build system:

```
-DSYN_VERSION_MAJOR=1 -DSYN_VERSION_MINOR=2 -DSYN_VERSION_PATCH=0
-DSYN_GIT_HASH=\"abc1234\"
-DSYN_APP_NAME=\"MyFirmware\"
```

## HMAC Firmware Verification

Opt-in HMAC-SHA256 image signing, layered on top of the existing CRC-32 integrity check. Gated by `SYN_FW_USE_HMAC` (default **off**) — platforms that don't need cryptographic verification pay nothing.

```c
// syn_config.h
#define SYN_FW_USE_HMAC 1   // Requires SYN_USE_SHA256 and SYN_USE_BOOT
```

When enabled, the `SYN_FwImageHeader` gains a 32-byte `image_hmac` field (header grows from 24 → 56 bytes). The update flow becomes:

```c
SYN_FwUpdate upd;
uint8_t page_buf[256];

// 1. Begin the update (zeroes the context)
syn_fwupdate_begin(&upd, SLOT_ADDR, MAX_SIZE, page_buf, sizeof(page_buf));

// 2. Set the HMAC key (must come after begin, before first write)
syn_fwupdate_set_key(&upd, hmac_key, 32);

// 3. Stream data chunks (CRC-32 + HMAC-SHA256 computed incrementally)
syn_fwupdate_write(&upd, chunk, chunk_len);

// 4. Finalize — verify CRC and HMAC, write header
syn_fwupdate_finish(&upd, expected_crc, expected_hmac, version_code);
```

### Verification is doubly opt-in

- **Compile-time:** `SYN_FW_USE_HMAC` must be `1`
- **Runtime:** Pass a non-NULL `expected_hmac` to `finish()`. Passing `NULL` skips HMAC verification and falls back to CRC-only — even when HMAC support is compiled in.

The HMAC comparison uses constant-time byte comparison to prevent timing side-channel attacks.

