# IR Sender Module (`IRSender.h/.cpp`)

This module sends NEC IR frames using ESP32 LEDC PWM output.

## Failure Codes

`sendCommand()` / `sendFrame()` can return:

- `NONE`
- `NOT_INITIALIZED` (called before `begin()`)
- `INVALID_COMMAND` (unknown enum or packet validation failure)
- `INVALID_CONFIG` (runtime config values invalid)
- `HW_UNAVAILABLE` (no Arduino hardware layer in current build)

## Initialization

`begin()` validates configuration from `prefferences.h` and initializes PWM:

- `ledcSetup(kIrPwmChannel, kIrCarrierFreqHz, kIrPwmResolutionBits)`
- `ledcAttachPin(kIrTxPin, kIrPwmChannel)`

Default carrier is 38 kHz (`kIrCarrierFreqHz = 38000`).

## Mark and Space

- `mark(durationUs)`: carrier ON (`ledcWrite(..., 128)` with 8-bit resolution)
- `space(durationUs)`: carrier OFF (`ledcWrite(..., 0)`)

With 8-bit resolution, duty `128` is approximately 50%.

## NEC Frame Timing

The implementation sends:

- header: `mark(9000)`, `space(4500)`
- 32 data bits (LSB-first per byte):
  - bit mark: `mark(560)`
  - bit `0` space: `space(560)`
  - bit `1` space: `space(1690)`
- trailer: `mark(560)`, `space(560)`

Packet byte order:

1. address
2. address second byte (`~address` for classic NEC, high address byte for extended NEC)
3. command
4. inverse command

## Safety Check Before Send

`sendFrame()` builds a packet via `protocol::makePacket(command)` and validates it with `protocol::parsePacket(...)` before transmitting. If validation fails, it returns `INVALID_COMMAND`.
