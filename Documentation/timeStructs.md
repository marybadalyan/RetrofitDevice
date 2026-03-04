# Time Structs and Clock (`time/wall_clock.h/.cpp`)

This module provides a consistent way to get time in firmware.

## Why It Exists

`millis()` and `micros()` only tell us uptime since boot.  
They do not provide real calendar time (date/hour).

`NtpClock` solves this by syncing once to Unix time, then continuing from uptime.

## Main Types

### `WallClockSnapshot`

A snapshot returned by the clock. It includes:

- boot-relative time: `bootMs`, `bootUs`
- validity flag: `valid`
- Unix time: `unixMs`
- local calendar fields: `year`, `month`, `day`, `hour`, `minute`, `second`, `weekday`
- helpers: `secondsOfDay`, `dateKey` (`YYYYMMDD`)

These fields are used directly by the scheduler for daily wall-clock triggers.

### `IClock`

Abstract clock interface:

- `isValid()`
- `now(nowMs, nowUs)`

This makes clock logic testable and replaceable (for example with `MockClock`).

### `NtpClock`

Concrete implementation used in firmware.

- `beginNtp(...)`: configures timezone and NTP servers (when supported by target/runtime).
- `setUnixTimeMs(unixMs, nowMs)`: sets a trusted base timestamp.
- `now(nowMs, nowUs)`: returns a `WallClockSnapshot`.

## How Scheduler Uses Time

`CommandScheduler` supports two scheduling modes:

- `RELATIVE_ONCE`: one-time command at a boot-relative `atMs`
- `DAILY_WALL_CLOCK`: daily command at `hour:minute:second` with a weekday mask

For daily entries, scheduler depends on these `WallClockSnapshot` fields:

- `valid` must be `true`
- `dateKey` identifies "today"
- `weekday` is matched against the entry weekday mask
- `secondsOfDay` is compared with the target daily time

To avoid duplicates, each daily entry stores `lastFiredDateKey` and will fire at most once per date.

Priority in `nextDueCommand(...)`:

1. due relative entry (earliest `atMs`)
2. due daily entry (earliest target time for current day)

## How `NtpClock::now()` Works

1. Stores input uptime (`nowMs`, `nowUs`) into the snapshot.
2. If not valid yet and NTP is enabled, it tries to refresh from system time.
3. If still invalid, returns snapshot with `valid=false`.
4. If valid, computes current Unix ms as:
   `baseUnixMs + (nowMs - baseNowMs)`.
5. Fills local calendar fields using `localtime_r` (if available).

## `refreshFromSystemTime()` Note

`refreshFromSystemTime()` reads `time(nullptr)` and accepts it only if it passes a sanity floor:

- `kUnixSanityFloor = 1700000000` seconds

This prevents using obviously invalid pre-sync system time.
