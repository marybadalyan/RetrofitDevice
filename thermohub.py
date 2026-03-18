#!/usr/bin/env python3
"""
thermohub.py  —  FastAPI hub for ESP32 thermostat
Run: uvicorn thermohub:app --host 0.0.0.0 --port 5000 --reload

Install:
  pip install fastapi uvicorn[standard] scikit-learn pandas numpy
"""

import json
import sqlite3
import logging
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional
from collections import defaultdict

import numpy as np
import pandas as pd
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from pydantic import BaseModel

# ── CONFIG ────────────────────────────────────────────────────
BASE_DIR = Path(__file__).resolve().parent
DB_PATH = BASE_DIR / "thermo.db"
DASHBOARD_HTML = BASE_DIR / "dashboard.html"  # served at /

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("thermohub")

# ── APP ───────────────────────────────────────────────────────
app = FastAPI(title="ThermoHub", version="1.0")
app.add_middleware(CORSMiddleware, allow_origins=["*"], allow_methods=["*"], allow_headers=["*"])

# ── DATABASE ─────────────────────────────────────────────────
def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    with get_db() as conn:
        conn.executescript("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            ts          TEXT    NOT NULL,
            room_temp   REAL,
            target_temp REAL,
            power       INTEGER,
            mode        TEXT,
            pid_p       REAL,
            pid_i       REAL,
            pid_d       REAL,
            pid_steps   INTEGER,
            integral    REAL
        );

        CREATE TABLE IF NOT EXISTS commands (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            ts      TEXT NOT NULL,
            command TEXT NOT NULL,
            source  TEXT DEFAULT 'hub'
        );

        CREATE TABLE IF NOT EXISTS schedule (
            id      INTEGER PRIMARY KEY AUTOINCREMENT,
            day     TEXT NOT NULL,
            time    TEXT NOT NULL,
            temp    REAL,
            type    TEXT NOT NULL DEFAULT 'temp',
            command TEXT
        );

        CREATE TABLE IF NOT EXISTS config (
            key   TEXT PRIMARY KEY,
            value TEXT
        );
        """)
    log.info("Database ready: %s", DB_PATH)

# ── MODELS ───────────────────────────────────────────────────
class TelemetryIn(BaseModel):
    room_temp:   Optional[float] = None
    target_temp: Optional[float] = None
    power:       Optional[bool]  = None
    mode:        Optional[str]   = None
    pid_p:       Optional[float] = None
    pid_i:       Optional[float] = None
    pid_d:       Optional[float] = None
    pid_steps:   Optional[int]   = None
    integral:    Optional[float] = None

class CommandIn(BaseModel):
    command: str   # 'on' | 'off' | 'temp_up' | 'temp_down'

class ScheduleEntry(BaseModel):
    day:     str
    time:    str
    type:    str = "temp"
    temp:    Optional[float] = None
    command: Optional[str]  = None

class ScheduleIn(BaseModel):
    schedule: list[ScheduleEntry]

class ConfigIn(BaseModel):
    ir_tx_pin:          Optional[int]   = None
    ir_rx_pin:          Optional[int]   = None
    led_red_pin:        Optional[int]   = None
    led_green_pin:      Optional[int]   = None
    led_blue_pin:       Optional[int]   = None
    wifi_ssid:          Optional[str]   = None
    wifi_password:      Optional[str]   = None
    pid_mode:           Optional[str]   = None
    pid_kp:             Optional[float] = None
    pid_ki:             Optional[float] = None
    pid_kd:             Optional[float] = None
    pid_max_steps:      Optional[int]   = None
    control_interval_s: Optional[int]   = None
    deadband_c:         Optional[float] = None

# ── IN-MEMORY DEVICE STATE ────────────────────────────────────
device_state = {
    "room_temp":   None,
    "target_temp": None,
    "power":       True,
    "mode":        "FAST",
    "pid":         {"p": 0, "i": 0, "d": 0, "steps": 0},
    "last_seen":   None,
}

# ─────────────────────────────────────────────────────────────
#  ROUTES
# ─────────────────────────────────────────────────────────────

@app.get("/")
def serve_dashboard():
    if DASHBOARD_HTML.exists():
        return FileResponse(DASHBOARD_HTML)
    return {"status": "ThermoHub running. Place dashboard.html next to thermohub.py"}

# ── ESP32: POST telemetry ──────────────────────────────────────
@app.post("/api/telemetry")
def post_telemetry(data: TelemetryIn):
    now = datetime.utcnow().isoformat()

    # -999 is the ESP32 sentinel for "no sensor connected" — treat as None
    NO_SENSOR = -999.0
    room_temp   = None if (data.room_temp   is None or data.room_temp   <= NO_SENSOR) else data.room_temp
    target_temp = None if (data.target_temp is None or data.target_temp <= NO_SENSOR) else data.target_temp

    # Update live state — only overwrite if real values received
    if room_temp   is not None: device_state["room_temp"]   = room_temp
    if target_temp is not None: device_state["target_temp"] = target_temp
    if data.power  is not None: device_state["power"]       = data.power
    if data.mode   is not None: device_state["mode"]        = data.mode
    device_state["pid"] = {
        "p": data.pid_p, "i": data.pid_i,
        "d": data.pid_d, "steps": data.pid_steps
    }
    device_state["last_seen"] = now

    # Only persist rows that have real sensor data — keeps history clean
    if room_temp is not None or target_temp is not None:
        with get_db() as conn:
            conn.execute("""
                INSERT INTO telemetry
                  (ts, room_temp, target_temp, power, mode, pid_p, pid_i, pid_d, pid_steps, integral)
                VALUES (?,?,?,?,?,?,?,?,?,?)
            """, (now, room_temp, target_temp,
                  int(data.power) if data.power is not None else None,
                  data.mode, data.pid_p, data.pid_i, data.pid_d, data.pid_steps, data.integral))

    # Check schedule for current slot
    action = get_scheduled_action_now()
    response = {"status": "ok"}

    if action:
        if action["type"] == "temp" and action["temp"] is not None and target_temp is not None:
            if abs(action["temp"] - target_temp) >= 0.5:
                response["scheduled_target"] = action["temp"]
                log.info("Schedule temp override: %.1f°C", action["temp"])

        elif action["type"] == "command" and action["command"]:
            with get_db() as conn:
                conn.execute(
                    "INSERT INTO commands (ts, command, source) VALUES (?,?,?)",
                    (datetime.utcnow().isoformat(), action["command"], "schedule")
                )
            log.info("Schedule command queued: %s", action["command"])

    return response

# ── Dashboard: GET current status ─────────────────────────────
@app.get("/api/status")
def get_status():
    return device_state

# ── Dashboard / ESP32: send command ───────────────────────────
@app.post("/api/command")
def post_command(body: CommandIn):
    valid = {"on", "off", "temp_up", "temp_down"}
    if body.command not in valid:
        raise HTTPException(400, f"Invalid command. Use one of: {valid}")

    # Update state immediately
    if body.command == "on":  device_state["power"] = True
    if body.command == "off": device_state["power"] = False

    with get_db() as conn:
        conn.execute("INSERT INTO commands (ts, command, source) VALUES (?,?,'dashboard')",
                     (datetime.utcnow().isoformat(), body.command))

    log.info("Command queued: %s", body.command)
    return {"status": "queued", "command": body.command}

# ── ESP32: poll for pending command ───────────────────────────
@app.get("/api/command/pending")
def get_pending_command():
    """
    ESP32 calls this after each telemetry POST.
    Returns the most recent unacknowledged command if any, then marks it done.
    """
    with get_db() as conn:
        row = conn.execute("""
            SELECT id, command FROM commands
            WHERE source IN ('dashboard','schedule')
              AND ts > datetime('now', '-30 seconds')
            ORDER BY id DESC LIMIT 1
        """).fetchone()
        if not row:
            return {"command": None}
        conn.execute("UPDATE commands SET source='sent' WHERE id=?", (row["id"],))
        return {"command": row["command"]}

# ── History ───────────────────────────────────────────────────
@app.get("/api/history")
def get_history(hours: int = 336):
    cutoff = (datetime.utcnow() - timedelta(hours=hours)).isoformat()
    with get_db() as conn:
        rows = conn.execute("""
            SELECT ts, room_temp, target_temp, power, pid_p, pid_i, pid_steps, integral
            FROM telemetry WHERE ts > ? ORDER BY ts ASC
        """, (cutoff,)).fetchall()
        cycles = conn.execute("""
            SELECT COUNT(*) as n FROM telemetry WHERE ts > ? AND pid_steps IS NOT NULL AND pid_steps != 0
        """, (cutoff,)).fetchone()["n"]

    readings = [dict(r) for r in rows]

    # Simple calibration estimate from integral history
    calibration = None
    if len(readings) >= 20:
        integrals = [r["integral"] for r in readings if r["integral"] is not None]
        if integrals:
            avg_integral = float(np.mean(integrals))
            # Estimate warm-up: higher mean integral = slower heater
            warmup = max(5, min(60, int(abs(avg_integral) * 3)))
            responsiveness = "fast" if abs(avg_integral) < 5 else "medium" if abs(avg_integral) < 15 else "slow"
            calibration = {
                "warmup_minutes": warmup,
                "responsiveness": responsiveness,
                "avg_integral": avg_integral
            }

    return {"readings": readings, "pid_cycles": cycles, "calibration": calibration}

# ── Schedule: GET ─────────────────────────────────────────────
@app.get("/api/schedule")
def get_schedule():
    with get_db() as conn:
        rows = conn.execute("SELECT day, time, type, temp, command FROM schedule ORDER BY day, time").fetchall()
    return {"schedule": [dict(r) for r in rows]}

# ── Schedule: POST (save from dashboard) ──────────────────────
@app.post("/api/schedule")
def save_schedule(body: ScheduleIn):
    with get_db() as conn:
        conn.execute("DELETE FROM schedule")
        conn.executemany(
            "INSERT INTO schedule (day, time, type, temp, command) VALUES (?,?,?,?,?)",
            [(e.day, e.time, e.type, e.temp, e.command) for e in body.schedule]
        )
    log.info("Schedule saved: %d entries", len(body.schedule))
    return {"status": "saved", "entries": len(body.schedule)}

# ── Schedule: Auto-generate from usage history ────────────────
@app.post("/api/schedule/generate")
def generate_schedule():
    """
    Analyse temperature history to find when the user typically wants
    heating ON and at what temperature, broken down by day-of-week and hour.
    """
    with get_db() as conn:
        rows = conn.execute("""
            SELECT ts, target_temp, power FROM telemetry
            WHERE power = 1 AND target_temp IS NOT NULL
            ORDER BY ts ASC
        """).fetchall()

    if len(rows) < 50:
        raise HTTPException(400, "Need at least 50 powered-on readings to generate a schedule")

    df = pd.DataFrame([dict(r) for r in rows])
    df["ts"] = pd.to_datetime(df["ts"], format='mixed')
    df["dow"] = df["ts"].dt.day_name().str[:3]   # Mon, Tue ...
    df["hour"] = df["ts"].dt.hour

    # For each day+hour bucket, find the median target temp
    pivot = df.groupby(["dow", "hour"])["target_temp"].median().reset_index()

    DAY_ORDER = ["Mon","Tue","Wed","Thu","Fri","Sat","Sun"]
    schedule = []

    for day in DAY_ORDER:
        day_data = pivot[pivot["dow"] == day].sort_values("hour")
        if day_data.empty:
            continue

        prev_temp = None
        for _, row in day_data.iterrows():
            t = round(float(row["target_temp"]) * 2) / 2  # round to nearest 0.5
            hour = int(row["hour"])
            time_str = f"{hour:02d}:00"

            # Only emit when the temperature actually changes
            if t != prev_temp:
                schedule.append({"day": day, "time": time_str, "temp": t})
                prev_temp = t

    # Add night setback if not already present
    for day in DAY_ORDER:
        has_night = any(e["day"] == day and e["time"] >= "22:00" for e in schedule)
        if not has_night:
            schedule.append({"day": day, "time": "22:30", "temp": 17.0})

    log.info("Auto-generated %d schedule entries from history", len(schedule))
    return {"schedule": schedule, "generated_from": len(rows)}

# ── Config: GET ───────────────────────────────────────────────
@app.get("/api/config")
def get_config():
    with get_db() as conn:
        rows = conn.execute("SELECT key, value FROM config").fetchall()
    cfg = {}
    for row in rows:
        try:    cfg[row["key"]] = json.loads(row["value"])
        except: cfg[row["key"]] = row["value"]
    return cfg

# ── Config: POST (save + push to ESP32 on next poll) ──────────
@app.post("/api/config")
def save_config(body: ConfigIn):
    data = body.model_dump(exclude_none=True)
    with get_db() as conn:
        conn.executemany("INSERT OR REPLACE INTO config (key, value) VALUES (?,?)",
                         [(k, json.dumps(v)) for k, v in data.items()])
    log.info("Config updated: %s", list(data.keys()))
    return {"status": "saved", "keys": list(data.keys())}

# ── ESP32: poll config (on boot) ──────────────────────────────
@app.get("/api/config/esp32")
def get_esp32_config():
    """
    ESP32 calls this on boot to get its runtime config.
    Returns only hardware-relevant keys.
    """
    with get_db() as conn:
        rows = conn.execute("SELECT key, value FROM config").fetchall()
    cfg = {}
    for row in rows:
        try:    cfg[row["key"]] = json.loads(row["value"])
        except: cfg[row["key"]] = row["value"]

    esp_keys = ["ir_tx_pin","ir_rx_pin","led_red_pin","led_green_pin","led_blue_pin",
                "wifi_ssid","wifi_password","pid_mode","pid_kp","pid_ki","pid_kd",
                "pid_max_steps","control_interval_s","deadband_c"]
    return {k: cfg[k] for k in esp_keys if k in cfg}

# ── Helpers ───────────────────────────────────────────────────
def get_scheduled_action_now() -> dict | None:
    """
    Return the current schedule slot if any.
    Returns {"type": "temp", "temp": 21.0} or {"type": "command", "command": "on"} or None.
    """
    now = datetime.now()
    day = now.strftime("%a")
    time_str = now.strftime("%H:%M")

    with get_db() as conn:
        rows = conn.execute("""
            SELECT time, type, temp, command FROM schedule
            WHERE day = ? AND time <= ?
            ORDER BY time DESC LIMIT 1
        """, (day, time_str)).fetchall()

    if not rows:
        return None
    row = rows[0]
    if row["type"] == "command":
        return {"type": "command", "command": row["command"]}
    return {"type": "temp", "temp": float(row["temp"]) if row["temp"] else None}

# ── STARTUP ───────────────────────────────────────────────────
@app.on_event("startup")
def startup():
    init_db()
    log.info("ThermoHub ready — visit http://localhost:5000")