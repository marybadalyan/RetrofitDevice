#!/usr/bin/env python3
"""
thermohub.py  —  FastAPI hub for ESP32 thermostat
Run: uvicorn thermohub:app --host 0.0.0.0 --port 5000 --reload

Install:
  pip install fastapi uvicorn[standard] scikit-learn pandas numpy
"""

import json
import csv
import time
import secrets
import sqlite3
import logging
from datetime import datetime, timedelta
from pathlib import Path
from typing import Optional
from collections import defaultdict

import numpy as np
import pandas as pd
from fastapi import FastAPI, HTTPException, Request, Form
from fastapi.middleware.cors import CORSMiddleware
from starlette.middleware.sessions import SessionMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, HTMLResponse, RedirectResponse
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

from starlette.middleware.sessions import SessionMiddleware
app.add_middleware(SessionMiddleware, secret_key=secrets.token_hex(32))

# ── DEVICE REGISTRY ──────────────────────────────────────────
DEVICES_FILE = BASE_DIR / "devices.csv"

def load_devices() -> dict:
    if not DEVICES_FILE.exists():
        log.warning("devices.csv not found — run generate_devices.py first")
        return {}
    devices = {}
    with open(DEVICES_FILE) as f:
        for row in csv.DictReader(f):
            devices[row["id"].upper()] = {
                "password": row["password"],
                "name":     row.get("name", f"Device {row['id']}"),
            }
    log.info("Loaded %d devices from registry", len(devices))
    return devices

DEVICES = load_devices()

# ── AUTH HELPERS ─────────────────────────────────────────────
# ── Login page: shown at / (device ID + password) ───────────
ENTRY_PAGE = """<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ThermoHub</title>
  <link href="https://fonts.googleapis.com/css2?family=DM+Serif+Display:ital@0;1&family=DM+Mono:wght@300;400;500&display=swap" rel="stylesheet">
  <style>
    *{{box-sizing:border-box;margin:0;padding:0}}
    :root{{--bg:#f5f0eb;--surface:#faf7f4;--card:#fff;--border:#e8e0d6;
          --text:#2a1f14;--muted:#9c8b7a;--accent:#c06c84;
          --serif:'DM Serif Display',serif;--mono:'DM Mono',monospace}}
    body{{background:var(--bg);color:var(--text);font-family:var(--mono);
         min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px}}
    .card{{background:var(--card);border:1px solid var(--border);border-radius:20px;
           padding:44px;width:100%;max-width:400px}}
    .logo{{font-family:var(--serif);font-size:1.4rem;margin-bottom:28px;display:block;
           letter-spacing:-0.5px}}
    .logo em{{color:var(--accent);font-style:italic}}
    h1{{font-family:var(--serif);font-size:2rem;margin-bottom:10px;letter-spacing:-0.5px}}
    p{{color:var(--muted);font-size:13px;line-height:1.6;margin-bottom:28px}}
    label{{display:block;font-size:11px;letter-spacing:2px;text-transform:uppercase;
           color:var(--muted);margin-bottom:8px}}
    input{{width:100%;background:var(--surface);border:1px solid var(--border);
           border-radius:10px;padding:13px 16px;font-family:var(--mono);font-size:14px;
           color:var(--text);outline:none;transition:border-color .2s;margin-bottom:16px}}
    input:focus{{border-color:var(--accent)}}
    button{{width:100%;background:var(--accent);color:#fff;border:none;border-radius:10px;
            padding:14px;font-family:var(--mono);font-size:12px;letter-spacing:1.5px;
            text-transform:uppercase;cursor:pointer;transition:opacity .2s}}
    button:hover{{opacity:.85}}
    .err{{background:rgba(192,108,132,.08);border:1px solid rgba(192,108,132,.25);
          color:var(--accent);border-radius:10px;padding:12px 16px;
          font-size:12px;margin-bottom:16px}}
  </style>
</head>
<body>
  <div class="card">
    <span class="logo">Thermo<em>Hub</em></span>
    <h1>Welcome back.</h1>
    <p>Enter your device ID and password from the card that came with your device.</p>
    {error}
    <form method="POST" action="/login">
      <label>Device ID</label>
      <input type="text" name="device_id" placeholder="e.g. YO4T2S"
             value="{device_id}" autocomplete="off"
             style="text-transform:uppercase" autofocus required>
      <label>Device password</label>
      <input type="password" name="password" placeholder="••••••••••" required>
      <button type="submit">Unlock Dashboard</button>
    </form>
  </div>
</body>
</html>"""

# ── Per-device login page: shown at /device/<id> ─────────────
LOGIN_PAGE = """<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ThermoHub — Device {device_id}</title>
  <link href="https://fonts.googleapis.com/css2?family=DM+Serif+Display:ital@0;1&family=DM+Mono:wght@300;400;500&display=swap" rel="stylesheet">
  <style>
    *{{box-sizing:border-box;margin:0;padding:0}}
    :root{{--bg:#f5f0eb;--surface:#faf7f4;--card:#fff;--border:#e8e0d6;
          --text:#2a1f14;--muted:#9c8b7a;--accent:#c06c84;
          --serif:'DM Serif Display',serif;--mono:'DM Mono',monospace}}
    body{{background:var(--bg);color:var(--text);font-family:var(--mono);
         min-height:100vh;display:flex;align-items:center;justify-content:center;padding:24px}}
    .card{{background:var(--card);border:1px solid var(--border);border-radius:20px;
           padding:44px;width:100%;max-width:400px}}
    .badge{{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:var(--muted);
            margin-bottom:28px;display:block}}
    h1{{font-family:var(--serif);font-size:2rem;margin-bottom:10px;letter-spacing:-0.5px}}
    p{{color:var(--muted);font-size:13px;line-height:1.6;margin-bottom:32px}}
    label{{display:block;font-size:11px;letter-spacing:2px;text-transform:uppercase;
           color:var(--muted);margin-bottom:8px}}
    input{{width:100%;background:var(--surface);border:1px solid var(--border);
           border-radius:10px;padding:13px 16px;font-family:var(--mono);font-size:14px;
           color:var(--text);outline:none;transition:border-color .2s;margin-bottom:16px}}
    input:focus{{border-color:var(--accent)}}
    button{{width:100%;background:var(--accent);color:#fff;border:none;border-radius:10px;
            padding:14px;font-family:var(--mono);font-size:12px;letter-spacing:1.5px;
            text-transform:uppercase;cursor:pointer;transition:opacity .2s}}
    button:hover{{opacity:.85}}
    .err{{background:rgba(192,108,132,.08);border:1px solid rgba(192,108,132,.25);
          color:var(--accent);border-radius:10px;padding:12px 16px;
          font-size:12px;margin-bottom:16px}}
  </style>
</head>
<body>
  <div class="card">
    <span class="badge">ThermoHub // {device_id}</span>
    <h1>Welcome back.</h1>
    <p>Enter the password that came with your device.</p>
    {error}
    <form method="POST">
      <label>Device password</label>
      <input type="password" name="password" placeholder="••••••••••" autofocus required>
      <button type="submit">Unlock Dashboard</button>
    </form>
  </div>
</body>
</html>"""

def is_authenticated(request: Request, device_id: str) -> bool:
    return request.session.get(f"auth_{device_id.upper()}") is True

# ── DATABASE ─────────────────────────────────────────────────
def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA busy_timeout = 5000")
    return conn

def init_db():
    with get_db() as conn:
        conn.execute("PRAGMA journal_mode=WAL")
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

        CREATE TABLE IF NOT EXISTS custom_buttons (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL UNIQUE,
            protocol    INTEGER NOT NULL,
            address     INTEGER NOT NULL,
            command     INTEGER NOT NULL,
            created_at  TEXT NOT NULL
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
    ir_only: bool = False  # if True, don't update target temp

class CustomButtonIn(BaseModel):
    name: str  # user-friendly name for the button

class CustomButtonLearnResult(BaseModel):
    protocol: int
    address: int
    command: int

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
    "room_temp":    None,
    "target_temp":  21.0,
    "power":        True,
    "mode":         "FAST",
    "pid":          {"p": 0, "i": 0, "d": 0, "steps": 0},
    "last_seen":    None,
    "auto_control": False,
}

# ── IR LEARN STATE ─────────────────────────────────────────────
# Tracks the last learn operation for the dashboard to poll.
# status: "idle" | "listening" | "ok" | "fail"
learn_state = {
    "status": "idle",
    "cmd":    None,
    "ts":     None,
    "learned": {},   # per-code learned status: {"on_off": True, "temp_up": True, …}
}
learning_custom_button_id: int = None  # Track which custom button is being learned

# ── MANUAL OVERRIDE TRACKING ─────────────────────────────────
# Tracks the last schedule slot that was active when user manually changed temp.
# Schedule only overrides again when a NEW slot becomes active after the manual change.
last_manual_temp_ts: float = 0.0          # when user last pressed +/-
last_manual_slot_key: str = ""            # which slot was active at that moment

# ── SCHEDULE COMMAND DEDUP ──────────────────────────────────
# Prevents command-type schedule entries from firing every 10s telemetry cycle.
# Each unique slot+command only fires once until the slot changes.
last_schedule_cmd_key: str = ""

# ─────────────────────────────────────────────────────────────
#  ROUTES
# ─────────────────────────────────────────────────────────────

@app.get("/")
def serve_root():
    return HTMLResponse(ENTRY_PAGE.format(error="", device_id=""))

@app.post("/login")
async def login_submit(request: Request):
    form = await request.form()
    device_id = form.get("device_id", "").strip().upper()
    password  = form.get("password", "").strip()

    if device_id not in DEVICES:
        return HTMLResponse(ENTRY_PAGE.format(
            error='<div class="err">Device ID not found — check the card that came with your device.</div>',
            device_id=device_id
        ), status_code=401)

    if password != DEVICES[device_id]["password"]:
        return HTMLResponse(ENTRY_PAGE.format(
            error='<div class="err">Incorrect password — check the card that came with your device.</div>',
            device_id=device_id
        ), status_code=401)

    request.session[f"auth_{device_id}"] = True
    return RedirectResponse(f"/device/{device_id}", status_code=303)

# ── Device login ──────────────────────────────────────────────
@app.get("/device/{device_id}")
def device_login_page(device_id: str, request: Request):
    device_id = device_id.upper()
    if device_id not in DEVICES:
        raise HTTPException(404, "Device not found")
    if is_authenticated(request, device_id):
        return FileResponse(DASHBOARD_HTML)
    return HTMLResponse(LOGIN_PAGE.format(device_id=device_id, error=""))

@app.post("/device/{device_id}")
async def device_login_submit(device_id: str, request: Request):
    device_id = device_id.upper()
    if device_id not in DEVICES:
        raise HTTPException(404, "Device not found")
    form = await request.form()
    password = form.get("password", "")
    if password == DEVICES[device_id]["password"]:
        request.session[f"auth_{device_id}"] = True
        return RedirectResponse(f"/device/{device_id}", status_code=303)
    error = '<div class="err">Incorrect password — check the card that came with your device.</div>'
    return HTMLResponse(LOGIN_PAGE.format(device_id=device_id, error=error), status_code=401)

@app.get("/device/{device_id}/logout")
def device_logout(device_id: str, request: Request):
    device_id = device_id.upper()
    request.session.pop(f"auth_{device_id}", None)
    return RedirectResponse(f"/device/{device_id}", status_code=303)

# ── Guard all API routes — ESP32 uses Authorization header, browser uses session
def require_auth(device_id: str, request: Request):
    device_id = device_id.upper()
    if device_id not in DEVICES:
        raise HTTPException(404, "Device not found")
    auth_header = request.headers.get("Authorization", "")
    if auth_header == DEVICES[device_id]["password"]:
        return device_id   # ESP32 auth via header
    if is_authenticated(request, device_id):
        return device_id   # Browser auth via session
    raise HTTPException(401, "Unauthorized")

# ── ESP32: POST telemetry ──────────────────────────────────────
@app.post("/api/telemetry")
def post_telemetry(data: TelemetryIn, request: Request):
    global last_schedule_cmd_key
    device_id = request.headers.get("X-Device-ID", "").upper()
    if device_id and device_id in DEVICES:
        auth_header = request.headers.get("Authorization", "")
        if auth_header != DEVICES[device_id]["password"]:
            raise HTTPException(401, "Unauthorized")
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
            conn.commit()

    # Check schedule for current slot
    action = get_scheduled_action_now()
    response = {"status": "ok", "auto_control": device_state["auto_control"]}

    # Push pid_mode config to ESP32 if it differs from what the device reported
    with get_db() as conn:
        row = conn.execute("SELECT value FROM config WHERE key='pid_mode'").fetchone()
    if row:
        cfg_mode = row["value"].strip('"').upper()  # stored as JSON string
        reported_mode = (data.mode or "FAST").upper()
        if cfg_mode in ("FAST", "ECO") and cfg_mode != reported_mode:
            response["pid_mode"] = cfg_mode
            log.info("Pushing mode change to ESP32: %s → %s", reported_mode, cfg_mode)

    if action:
        if action["type"] == "temp" and action["temp"] is not None:
            # Build current slot key — only override if we're in a NEW slot since manual change
            now_local = datetime.now()
            current_slot_key = f"{now_local.strftime('%a')}_{action.get('slot_time', 'none')}"
            manual_was_in_this_slot = (current_slot_key == last_manual_slot_key)

            if not manual_was_in_this_slot:
                sched_temp = action["temp"]
                need_update = target_temp is None or abs(sched_temp - target_temp) >= 0.5

                # Auto-enable PID when a temp schedule fires
                if not device_state["auto_control"]:
                    device_state["auto_control"] = True
                    with get_db() as conn:
                        conn.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('auto_control', ?)",
                                     (json.dumps(True),))
                        conn.commit()
                    response["auto_control"] = True
                    log.info("Schedule auto-enabled PID control")

                # Turn heater on if it's off
                if not device_state["power"]:
                    device_state["power"] = True
                    with get_db() as conn:
                        conn.execute("INSERT INTO commands (ts, command, source) VALUES (?,?,?)",
                                     (datetime.utcnow().isoformat(), "on", "schedule"))
                        conn.commit()
                    log.info("Schedule turned heater ON")

                if need_update:
                    response["scheduled_target"] = sched_temp
                    device_state["target_temp"] = sched_temp
                    with get_db() as conn:
                        conn.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('target_temp', ?)",
                                     (json.dumps(sched_temp),))
                        conn.commit()
                    log.info("Schedule temp override: %.1f°C (slot: %s)", sched_temp, current_slot_key)
            else:
                log.debug("Schedule suppressed — user manually changed temp in slot %s", current_slot_key)

        elif action["type"] == "command" and action["command"]:
            # Dedup: only fire each command slot once
            now_local = datetime.now()
            cmd_slot_key = f"{now_local.strftime('%a')}_{action.get('slot_time', 'none')}_{action['command']}"
            if cmd_slot_key != last_schedule_cmd_key:
                last_schedule_cmd_key = cmd_slot_key
                with get_db() as conn:
                    conn.execute(
                        "INSERT INTO commands (ts, command, source) VALUES (?,?,?)",
                        (datetime.utcnow().isoformat(), action["command"], "schedule")
                    )
                    conn.commit()
                log.info("Schedule command queued: %s", action["command"])

    return response

# ── Dashboard: GET current status ─────────────────────────────
@app.get("/api/status")
def get_status():
    return device_state

# ── Dashboard: toggle auto control (PID) ──────────────────────
class AutoControlIn(BaseModel):
    enabled: bool

@app.post("/api/autocontrol")
def set_auto_control(body: AutoControlIn):
    device_state["auto_control"] = body.enabled
    with get_db() as conn:
        conn.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('auto_control', ?)",
                     (json.dumps(body.enabled),))
        conn.commit()
    log.info("Auto control %s", "enabled" if body.enabled else "disabled")
    return {"status": "ok", "auto_control": body.enabled}

# ── Dashboard / ESP32: send command ───────────────────────────
@app.post("/api/command")
def post_command(body: CommandIn):
    global last_manual_temp_ts, last_manual_slot_key, learn_state
    valid = {"on", "off", "temp_up", "temp_down",
             "learn_on_off", "learn_temp_up", "learn_temp_down", "learn_clear"}
    if body.command not in valid:
        raise HTTPException(400, f"Invalid command. Use one of: {valid}")

    # Handle learn commands — update learn_state so dashboard can poll it
    if body.command in ("learn_on_off", "learn_temp_up", "learn_temp_down"):
        # Flush any older pending learn commands so the ESP32 doesn't pick
        # up stale ones after the current learn finishes.
        with get_db() as conn:
            conn.execute(
                "UPDATE commands SET source='sent' "
                "WHERE command LIKE 'learn_%' AND source IN ('dashboard','schedule')"
            )
            conn.commit()
        learn_state["status"] = "listening"
        learn_state["cmd"]    = body.command.replace("learn_", "")   # "on_off" not "learn_on_off"
        learn_state["ts"]     = time.time()
        log.info("Learn command queued: %s (tracking as %s)", body.command, learn_state["cmd"])
        # fall through to enqueue as a regular command below

    if body.command == "learn_clear":
        learn_state["status"] = "idle"
        learn_state["cmd"]    = None
        learn_state["ts"]     = None

    # Update state immediately
    if body.command == "on":  device_state["power"] = True
    if body.command == "off": device_state["power"] = False

    # Update target immediately so dashboard reflects change without waiting for ESP
    # (skip if ir_only flag is set — for IR commands that shouldn't change target)
    if not body.ir_only:
        if body.command == "temp_up":
            current = device_state["target_temp"] or 21.0
            device_state["target_temp"] = round(current + 0.5, 1)
            last_manual_temp_ts = datetime.now().timestamp()
            slot = get_scheduled_action_now()
            now = datetime.now()
            last_manual_slot_key = f"{now.strftime('%a')}_{slot['slot_time'] if slot else 'none'}"
            log.info("Manual target → %.1f°C (slot key: %s)", device_state["target_temp"], last_manual_slot_key)

        if body.command == "temp_down":
            current = device_state["target_temp"] or 21.0
            device_state["target_temp"] = round(current - 0.5, 1)
            last_manual_temp_ts = datetime.now().timestamp()
            slot = get_scheduled_action_now()
            now = datetime.now()
            last_manual_slot_key = f"{now.strftime('%a')}_{slot['slot_time'] if slot else 'none'}"
            log.info("Manual target → %.1f°C (slot key: %s)", device_state["target_temp"], last_manual_slot_key)

    if body.command in ("temp_up", "temp_down") and not body.ir_only:
        with get_db() as conn:
            conn.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('target_temp', ?)",
                         (json.dumps(device_state["target_temp"]),))
            conn.commit()

    with get_db() as conn:
        conn.execute("INSERT INTO commands (ts, command, source) VALUES (?,?,'dashboard')",
                     (datetime.utcnow().isoformat(), body.command))
        conn.commit()

    log.info("Command queued: %s", body.command)
    return {"status": "queued", "command": body.command}

# ── ESP32: poll for pending command ───────────────────────────
@app.get("/api/command/pending")
def get_pending_command():
    """
    ESP32 calls this after each telemetry POST.
    Returns the most recent unacknowledged command if any, then marks it done.
    For custom buttons, resolves the button ID to raw IR data so the device
    can send it directly without needing to store codes locally.
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
        conn.commit()

        cmd = row["command"]

        # Resolve custom button to raw IR data
        if cmd.startswith("custom_"):
            try:
                button_id = int(cmd.split("_", 1)[1])
                btn = conn.execute(
                    "SELECT name, protocol, address, command FROM custom_buttons WHERE id=?",
                    (button_id,)
                ).fetchone()
                if btn and not (btn["protocol"] == 0 and btn["address"] == 0 and btn["command"] == 0):
                    return {
                        "command": "send_ir",
                        "protocol": btn["protocol"],
                        "address": btn["address"],
                        "ir_command": btn["command"],
                        "name": btn["name"]
                    }
            except (ValueError, IndexError):
                pass
            return {"command": None}

        return {"command": cmd}

# ── IR Learn: GET status (dashboard polls this) ───────────────
@app.get("/api/learn/status")
def get_learn_status(ack: bool = False):
    global learn_state
    current = learn_state.copy()
    
    # Only reset to idle if the dashboard sends an 'ack'
    if ack and learn_state["status"] in ["ok", "fail"]:
        learn_state["status"] = "idle"
        learn_state["cmd"] = None
        log.info("Learn state cleared by dashboard ACK")
        
    return current
# ── IR Learn: POST result (ESP32 reports back) ────────────────
class LearnResultIn(BaseModel):
    cmd:     str   # "on_off" | "temp_up" | "temp_down" | "learn_custom"
    status:  str   # "ok" | "fail"
    protocol: Optional[int] = None  # IR protocol (for custom buttons)
    address:  Optional[int] = None  # IR address
    command:  Optional[int] = None  # IR command

@app.post("/api/learn/result")
def post_learn_result(body: LearnResultIn):
    global learn_state, learning_custom_button_id
    learn_state["status"] = body.status
    learn_state["cmd"]    = body.cmd
    learn_state["ts"]     = time.time()
    log.info("Learn result received: cmd=%s status=%s", body.cmd, body.status)

    # Track which factory codes have been successfully learned
    if body.status == "ok" and body.cmd in ("on_off", "temp_up", "temp_down"):
        learn_state["learned"][body.cmd] = True
        with get_db() as conn:
            conn.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('learned_codes', ?)",
                         (json.dumps(learn_state["learned"]),))
            conn.commit()

    # If learning a custom button and got a successful result, save it
    if body.cmd == "learn_custom" and body.status == "ok" and learning_custom_button_id:
        if body.protocol is not None and body.address is not None and body.command is not None:
            with get_db() as conn:
                row = conn.execute("SELECT name FROM custom_buttons WHERE id=?", (learning_custom_button_id,)).fetchone()
                if row:
                    conn.execute(
                        "UPDATE custom_buttons SET protocol=?, address=?, command=? WHERE id=?",
                        (body.protocol, body.address, body.command, learning_custom_button_id)
                    )
                    conn.commit()
                    log.info("Custom button saved: id=%d name=%s (proto=%d addr=0x%04X cmd=0x%04X)",
                             learning_custom_button_id, row["name"], body.protocol, body.address, body.command)
            learning_custom_button_id = None

    return {"status": "ok"}

# ── Custom Buttons ────────────────────────────────────────────
@app.post("/api/custom-buttons")
def create_custom_button(body: CustomButtonIn):
    """Create a new custom button (user will learn the code next)."""
    with get_db() as conn:
        try:
            conn.execute(
                "INSERT INTO custom_buttons (name, protocol, address, command, created_at) VALUES (?,?,?,?,?)",
                (body.name, 0, 0, 0, datetime.utcnow().isoformat())
            )
            conn.commit()
            row = conn.execute("SELECT id FROM custom_buttons WHERE name=?", (body.name,)).fetchone()
            log.info("Custom button created: %s (id=%d)", body.name, row["id"])
            return {"status": "ok", "id": row["id"], "name": body.name}
        except sqlite3.IntegrityError:
            raise HTTPException(400, f"Button name '{body.name}' already exists")

@app.get("/api/custom-buttons")
def list_custom_buttons():
    """List all custom buttons."""
    with get_db() as conn:
        rows = conn.execute("SELECT id, name, protocol, address, command FROM custom_buttons ORDER BY id").fetchall()
    return {"buttons": [dict(r) for r in rows]}

@app.post("/api/custom-buttons/clear-ir")
def clear_custom_buttons_ir():
    """Reset IR data on all custom buttons (keeps the buttons themselves)."""
    with get_db() as conn:
        conn.execute("UPDATE custom_buttons SET protocol=0, address=0, command=0")
        conn.commit()
    log.info("All custom button IR codes cleared")
    return {"status": "ok"}

@app.delete("/api/custom-buttons/{button_id}")
def delete_custom_button(button_id: int):
    """Delete a custom button."""
    with get_db() as conn:
        conn.execute("DELETE FROM custom_buttons WHERE id=?", (button_id,))
        conn.commit()
    log.info("Custom button deleted: id=%d", button_id)
    return {"status": "ok"}

@app.post("/api/custom-buttons/{button_id}/learn")
def learn_custom_button(button_id: int):
    """Start learning for a custom button."""
    global learning_custom_button_id, learn_state
    with get_db() as conn:
        row = conn.execute("SELECT name FROM custom_buttons WHERE id=?", (button_id,)).fetchone()
    if not row:
        raise HTTPException(404, "Button not found")
    learning_custom_button_id = button_id

    # Update learn_state so dashboard polling sees "listening" (not stale "ok")
    learn_state["status"] = "listening"
    learn_state["cmd"]    = "learn_custom"
    learn_state["ts"]     = time.time()

    # Flush stale learn commands then queue the new one
    with get_db() as conn:
        conn.execute(
            "UPDATE commands SET source='sent' "
            "WHERE command LIKE 'learn_%' AND source IN ('dashboard','schedule')"
        )
        conn.execute("INSERT INTO commands (ts, command, source) VALUES (?,?,'dashboard')",
                     (datetime.utcnow().isoformat(), "learn_custom"))
        conn.commit()
    log.info("Starting learn for custom button: id=%d name=%s", button_id, row["name"])
    return {"status": "ok", "id": button_id, "name": row["name"]}

@app.post("/api/custom-buttons/{button_id}/send")
def send_custom_button(button_id: int):
    """Send IR command from a custom button."""
    with get_db() as conn:
        row = conn.execute("SELECT name, protocol, address, command FROM custom_buttons WHERE id=?", (button_id,)).fetchone()
    if not row:
        raise HTTPException(404, "Button not found")
    if row["protocol"] == 0 and row["address"] == 0 and row["command"] == 0:
        raise HTTPException(400, "Button not yet learned")

    # Queue as a custom IR command (send only, no other effects)
    with get_db() as conn:
        conn.execute("INSERT INTO commands (ts, command, source) VALUES (?,?,?)",
                     (datetime.utcnow().isoformat(), f"custom_{button_id}", "dashboard"))
        conn.commit()
    log.info("Custom button queued: %s (proto=%d addr=0x%04X cmd=0x%04X)",
             row["name"], row["protocol"], row["address"], row["command"])
    return {"status": "queued", "name": row["name"], "protocol": row["protocol"],
            "address": row["address"], "command": row["command"]}

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
        conn.commit()
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
        conn.commit()
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
    Returns {"type": "temp", "temp": 21.0, "slot_time": "07:00"} or
            {"type": "command", "command": "on", "slot_time": "07:00"} or None.
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
        return {"type": "command", "command": row["command"], "slot_time": row["time"]}
    return {"type": "temp", "temp": float(row["temp"]) if row["temp"] else None, "slot_time": row["time"]}

# ── STARTUP ───────────────────────────────────────────────────
@app.on_event("startup")
def startup():
    init_db()
    with get_db() as conn:
        for key in ("auto_control", "target_temp"):
            row = conn.execute("SELECT value FROM config WHERE key=?", (key,)).fetchone()
            if row is not None:
                device_state[key] = json.loads(row["value"])
        # Restore per-code learned status
        row = conn.execute("SELECT value FROM config WHERE key='learned_codes'").fetchone()
        if row is not None:
            learn_state["learned"] = json.loads(row["value"])
            log.info("Restored learned codes: %s", learn_state["learned"])
    log.info("ThermoHub ready — visit http://localhost:5000")