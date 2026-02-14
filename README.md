# RetrofitDevice

This repo now contains two separate ESP32 firmware targets that share core modules.

## Shared modules
- `commands.h`
- `protocol.h/.cpp`
- `logger.h/.cpp`
- `IRSender.h/.cpp`
- `IRReciever.h/.cpp`
- `prefferences.h`

## Firmware A: Retrofit Controller ESP32
- Entry point: `RetrofitDevice/main.cpp`
- Responsibility: scheduler/hub command source, send IR commands, wait for ACK, retry/log.

## Firmware B: Heater ESP32
- Entry point: `RetrofitDevice/heater/main.cpp`
- Responsibility: receive IR command, apply heater behavior, drive relay, send ACK.

## Important
Do not flash the same `main.cpp` to both boards. Each ESP32 must use its own entrypoint above.
