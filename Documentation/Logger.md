# Logger Module (`logger.h/.cpp`)

`Logger` stores a circular history of system events and optionally persists it to ESP32 NVS flash.

## What Gets Logged

Each `LogEntry` contains:

- boot-relative time (`uptimeMs`, `uptimeUs`)
- wall-clock fields from `WallClockSnapshot` (`unixMs`, `dateKey`, `hour`, `minute`, `second`, `weekday`, `wallTimeValid`)
- event metadata (`type`, `command`, `success`, `detailCode`)

## Event Types

`LogEventType` supports:

- `COMMAND_SENT`
- `COMMAND_DROPPED`
- `HUB_COMMAND_RX`
- `SCHEDULE_COMMAND`
- `STATE_CHANGE`
- `THERMOSTAT_CONTROL`
- `TRANSMIT_FAILED`
- `IR_FRAME_RX`

## Runtime Behavior

- Capacity is fixed: `Logger::kCapacity = 128`.
- New entries are written into `entries_[nextIndex_]`.
- `nextIndex_` wraps with modulo, so the buffer is circular.
- `size_` grows up to capacity, then stays at 128.
- If diagnostics level is high enough (`kDiagnosticsLogLevel >= 2`), each entry is printed to serial/stdout.

## Persistence Behavior

Persistence is enabled by calling:

```cpp
beginPersistence("namespace");
```

When `Preferences` is available:

- A persistent header is read from key `"header"` (`version`, `nextIndex`, `size`).
- The entries array is read from key `"entries"`.
- Version mismatch or corrupted size resets the in-memory log state.

On each `log(...)`, `persistState()` writes:

- `"header"`: metadata (`version`, `nextIndex`, `size`)
- `"entries"`: full `std::array<LogEntry, 128>`

## Notes

- Persistence is optional. If `Preferences` is unavailable, logging still works in RAM.
- Current implementation writes the full buffer on each log call. This is simple and robust, but increases flash write frequency.
