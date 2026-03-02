# Protocol Module (`protocol.h/.cpp`)

This module converts between internal commands and NEC packet bytes, and validates received packets.

## Packet Format

The code uses a 4-byte NEC packet:

- byte 0: address low byte
- byte 1: address inverse (`~address`) for classic NEC, or address high byte for extended NEC
- byte 2: command byte
- byte 3: command inverse (`~command`)

Represented as:

```cpp
struct Packet {
    uint8_t address;
    uint8_t addressInverse;
    uint8_t command;
    uint8_t commandInverse;
};
```

## Classic vs Extended NEC

Address source: `kNecDeviceAddress` in `prefferences.h`.

- Classic NEC is treated as `addressHigh == ~addressLow`.
- Extended NEC is treated as a full 16-bit address (`addressLow`, `addressHigh`).

`parsePacket()` detects which format is in use from the packet and validates accordingly.

## Command Mapping

Internal command enum (`commands.h`) is mapped to command bytes from `prefferences.h`:

- `ON` -> `kNecCommandOn`
- `OFF` -> `kNecCommandOff`
- `TEMP_UP` -> `kNecCommandTempUp`
- `TEMP_DOWN` -> `kNecCommandTempDown`

## Public API

- `encodeCommand(Command, uint8_t&)`: enum -> NEC command byte
- `decodeCommand(uint8_t, Command&)`: NEC command byte -> enum
- `makePacket(Command)`: build transmit packet (includes inverse bytes)
- `parsePacket(const Packet&, Command&)`: validate packet and decode command

`parsePacket()` returns `false` if address check fails, command inverse check fails, or command byte is not recognized.
