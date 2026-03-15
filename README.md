# RetrofitDevice

This repository contains two ESP32 firmware targets that communicate using the NEC infrared protocol and share common modules.

## Firmware Targets

- Retrofit controller firmware: `RetrofitDevice/main.cpp`
- Heater firmware: `RetrofitDevice/heater/main.cpp`

Do not flash the same entrypoint to both boards.

## Shared Modules

- `commands.h`: command enum (`ON`, `OFF`, `TEMP_UP`, `TEMP_DOWN`)
- `protocol.h/.cpp`: encode/decode and validate NEC packets
- `IRSender.h/.cpp`: transmit NEC frames
- `IRReciever.h/.cpp`: receive and decode NEC frames
- `logger.h/.cpp`: circular log buffer with optional flash persistence
- `prefferences.h`: pins, NEC command bytes, and runtime constants

## NEC Command Mapping

The physical remote command bytes are configured in `prefferences.h`:

- `kNecDeviceAddress`
- `kNecCommandOn`
- `kNecCommandOff`
- `kNecCommandTempUp`
- `kNecCommandTempDown`

`protocol::decodeCommand()` maps received NEC command bytes to the internal `Command` enum.

## Build and Test

- Run host tests: `pio test -e native_test`
- Run local test helper: `./scripts/test_local.sh`
- Flash and monitor: `./scripts/flash_and_monitor.sh retrofit 115200`
