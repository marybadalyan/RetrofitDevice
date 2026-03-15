# IR Receiver Module (`IRReciever.h/.cpp`)

This module captures raw IR edge timings via GPIO interrupts and decodes NEC frames into `Command`.

## Initialization

`begin()` configures receive pin input and registers an interrupt:

```cpp
pinMode(kIrRxPin, INPUT);
attachInterrupt(digitalPinToInterrupt(kIrRxPin), &IRReceiver::isrThunk, CHANGE);
```

`CHANGE` means the ISR runs on both rising and falling edges.

## ISR Path

- `isrThunk()` is a static bridge required by `attachInterrupt`.
- It forwards to the active object (`activeInstance_->onEdgeInterrupt()`).
- `onEdgeInterrupt()` measures elapsed time from last edge (`micros()` delta).
- Valid pulse durations are appended to `pulseDurationsUs_` until capacity (128).

Guard rules in ISR:

- `deltaUs > 14000` -> start a new frame (`pulseCount_ = 0`)
- `deltaUs < 80` -> ignore as noise
- never write past array bounds

## Decoding Path

`poll()` performs these steps:

1. Disable interrupts temporarily.
2. Copy `pulseDurationsUs_` and `pulseCount_` to local storage.
3. Re-enable interrupts quickly.
4. Decode from the local copy.

This avoids data races because ISR can modify shared pulse buffers at any time.

`decodeFrame()` expects NEC timing:

- header mark: ~9000 us
- header space: ~4500 us
- bit mark: ~560 us
- bit 0 space: ~560 us
- bit 1 space: ~1690 us
- tolerance: +/-300 us

Then it reconstructs 4 bytes:

- `[address][address second byte][command][~command]`

Finally it calls `protocol::parsePacket()` to validate address and command inverse, then returns decoded `Command`.

## Diagnostics

If `kDiagnosticsLogLevel >= 2`, successful decodes are printed (`[IR-RX] decoded cmd=...`).
