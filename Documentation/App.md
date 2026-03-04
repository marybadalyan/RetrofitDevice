# App Folder Overview (`app/`)

This folder contains the main control logic for the retrofit controller firmware.

## Files and Roles

- `retrofit_controller.h/.cpp`
  - Orchestrates command intake, thermostat state, PID control, and IR transmit.
  - Hub commands are checked first, scheduler commands second.
  - Logs source events, thermostat control actions, transmit success/failure.

- `pid_thermostat_controller.h/.cpp`
  - Runs periodic PID cycles (`controlIntervalMs`).
  - Uses mode-specific base tuning (`FAST` / `ECO`).
  - Produces signed step count:
    - positive steps -> `TEMP_UP`
    - negative steps -> `TEMP_DOWN`
  - Includes deadband, integral clamp (anti-windup), and step clamp.

- `adaptive_thermostat_tuning.h/.cpp`
  - Learns a measured heating effectiveness over evaluation windows.
  - Adjusts aggressiveness scale over time.
  - Outputs runtime overrides for PID (`kp`, `maxSteps`) within safe bounds.

- `room_temp_sensor.h/.cpp`
  - Temperature source abstraction.
  - Current implementation is a mock drift model when no real sensor exists.

## `RetrofitController::tick(...)` Flow

1. Poll next command source:
   - hub receiver first (priority)
   - scheduler second
2. Apply command to internal state:
   - `ON` / `OFF` toggles power
   - `TEMP_UP` / `TEMP_DOWN` adjusts target setpoint
3. Run thermostat loop if power is enabled:
   - get base PID tuning for mode
   - apply adaptive overrides
   - run PID cycle
   - convert PID steps to repeated IR commands
4. Log every action and TX result.

## Why This Split Exists

- `RetrofitController` stays focused on orchestration.
- PID math is isolated and testable.
- Adaptive tuning can evolve without changing command routing.
- Sensor source can be swapped (mock vs real hardware) without changing controller logic.
