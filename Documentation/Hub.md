# Hub Folder Overview (`hub/`)

This folder contains modules that feed commands into the retrofit controller from "hub-side" sources (network, mocks, and analytics).

## Files and Roles

- `hub_receiver.h/.cpp`
  - Small FIFO command queue (`pushMockCommand`, `poll`).
  - Used as the handoff point between hub producers and `RetrofitController`.

- `hub_connectivity.h/.cpp`
  - Manages Wi-Fi startup/reconnect.
  - Configures NTP time on first successful connection.
  - Exposes status flags: `wifiConnected()` and `timeConfigured()`.
  - Current implementation can also try timezone lookup via HTTP before NTP setup.

- `hub_mock_scheduler.h/.cpp`
  - Generates fallback hub commands when real cloud/hub path is unavailable.
  - Default behavior:
    - boot fallback `ON` after a short delay when wall time is invalid
    - daily `ON` at 07:00 and `OFF` at 22:00
  - Pushes commands into `HubReceiver`.

- `hub_ai_insights.h/.cpp`
  - Offline analytics over temperature/control history.
  - Detects patterns like overshoot, oscillation, possible window-open drops, and possible hardware failure.
  - Produces advisory PID scaling recommendations (`kpScale`, `kiScale`).

## Hub AI Insights (More Detail)

Absolutely, Mary — let’s break it down step by step so it’s crystal clear how **PID** fits into this heating system.

---

# **1️⃣ What is PID?**

PID stands for **Proportional, Integral, Derivative** — a type of **control algorithm** used to automatically regulate a system, like your heater.

It tries to make the **actual value** (room temperature) match the **desired value** (target temperature) by adjusting the output (heater power, in this case).

### **The three components**

1. **Proportional (P)**

   * Responds to the **current error**:
     [
     P = K_p \cdot \text{error}
     ]
   * Big error → stronger correction
   * In our code: `kpScale` adjusts the proportional strength.

2. **Integral (I)**

   * Responds to **accumulated past errors**:
     [
     I = K_i \cdot \sum \text{error over time}
     ]
   * Helps correct **steady-state offset** (like if the room never quite reaches the target).
   * In our code: `kiScale` adjusts this contribution.

3. **Derivative (D)** *(optional here)*

   * Responds to **rate of change of error**:
     [
     D = K_d \cdot \frac{d(\text{error})}{dt}
     ]
   * Predicts future error and prevents overshoot.
   * In `HubAiInsights`, we don’t explicitly calculate D — only P and I are used for recommendations.

---

# ** How `HubAiInsights` uses PID**

`HubAiInsights` **doesn’t directly control the heater**, it analyzes past heater behavior and **recommends PID adjustments** based on:

1. **Overshoot** → room temp went above target → reduce `kpScale` to prevent overreaction
2. **Oscillation** → temperature keeps crossing the target → reduce `kpScale`
3. **Slow response** → heater too slow to reach target → increase `kpScale`
4. **Steady-state error** → room not reaching target consistently → increase `kiScale`

```cpp
out.kpScale = insights_.kp_scale;  // proportional scaling
out.kiScale = insights_.ki_scale;  // integral scaling
```

* `kpScaleComfort_` → PID proportional tuning for comfort mode
* `kpScaleEco_` → proportional tuning for ECO mode
* `kiScale_` → integral tuning (same for both modes)

The system **observes heating/cooling patterns over time** with EMA (exponential moving average) to smooth noise and make recommendations that actually reflect real behavior, not random spikes.

---

# ** Why this approach?**

* The heater is **slow and noisy** — temperature changes gradually.
* Direct D-term might overreact, so we rely mostly on P and I.
* EMA allows the AI to **learn system behavior** gradually and recommend PID tuning.
* ECO vs COMFORT modes get **different recommendations** depending on comfort vs energy-saving trade-offs.

---

# **4️ In practice**

1. You run your heater.
2. Feed each reading (`LogEntry`) into `ingest()`.
3. The AI detects patterns: overshoot, slow heating, oscillations.
4. Call `recommendation()` → gives PID scaling suggestions:

```cpp
PidRecommendation rec = hubAi.recommendation();
heater.setPID(rec.kpScale, rec.kiScale);
```

* This allows the **existing PID controller** in your heater to perform better without manually tuning knobs.




`HubAiInsights` does not control hardware directly.  
It learns from telemetry samples (`LogEntry`) and outputs advisory signals.

Each ingested sample includes:

- `timestampMs`
- `roomTemperatureC`
- `targetTemperatureC`
- `commandSent` (`NONE`, `HEAT_UP`, `HEAT_DOWN`)
- `pidOutput`
- `mode` (`COMFORT`, `ECO`, `BOOST`)

Main things it estimates/detects:

- heating/cooling rate (EMA-based)
- repeated overshoot
- oscillation (frequent target crossings)
- likely window-open rapid cooling
- possible hardware failure (repeated weak heating windows despite `HEAT_UP`)

Output APIs:

- `insights()`: current learned metrics and anomaly flags
- `recommendation()`: advisory PID scaling (`kpScale`, `kiScale`)
- `probableHardwareCause()`: readable hint when failure is detected

Important behavior:

- recommendations are intentionally advisory-only (not automatic hard overrides)
- confidence grows with sample volume and stable learned rates
- ECO mode tends to recommend slightly lower aggressiveness than COMFORT
- testing should use synthetic thermal profiles (strong heater, weak heater, and flat/constant data),
  not only constant inputs, so learning/detection logic is validated against changing dynamics

## Runtime Flow

1. Connectivity layer brings Wi-Fi up and configures wall-clock time.
2. Real hub inputs and/or mock scheduler push commands to `HubReceiver`.
3. `RetrofitController` polls `HubReceiver` each tick.
4. If a hub command exists, it has priority over scheduler command for that tick.

## Why This Split Exists

- Keeps networking, command transport, fallback scheduling, and analytics separated.
- Makes each piece easier to test independently.
- Allows swapping hub transport implementation later without changing controller logic.
