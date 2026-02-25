# RetrofitDevice

This repo contains two ESP32 firmware targets that share core modules and use NEC IR protocol.

## Shared modules
- `commands.h`
- `protocol.h/.cpp`
- `logger.h/.cpp`
- `IRSender.h/.cpp`
- `IRReciever.h/.cpp`
- `prefferences.h`

## Firmware A: Retrofit Controller ESP32
- Entry point: `RetrofitDevice/main.cpp`
- Responsibility: scheduler/hub command source, send NEC IR commands, and log system activity.

## Firmware B: Heater ESP32
- Entry point: `RetrofitDevice/heater/main.cpp`
- Responsibility: receive NEC IR commands, apply heater behavior, and drive relay output.

## Physical Remote Controller
- No ESP32 firmware target is used for a remote controller anymore.
- Use a physical NEC remote and map your 4 control buttons by editing NEC values in `prefferences.h`:
  - `kNecDeviceAddress`
  - `kNecCommandOn`, `kNecCommandOff`, `kNecCommandTempUp`, `kNecCommandTempDown`

## Important
Do not flash the same `main.cpp` to both boards. Each ESP32 must use its own entrypoint above.

## Testing
- Run unit/integration tests on host: `pio test -e native_test`
- Easy local test command: `./scripts/test_local.sh`
- Build firmwares in CI: `retrofit`, `heater`
- CI also runs native tests and uploads `test-output.log` artifact
- Easy flash + logs from hardware: `./scripts/flash_and_monitor.sh retrofit 115200`
