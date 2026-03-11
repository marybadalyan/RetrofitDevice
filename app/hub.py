#!/usr/bin/env python3
"""
============================================================
  IoT AI Hub  —  hub.py
  Run this on your PC or Raspberry Pi.

  What it does:
    1. Subscribes to MQTT topics from ESP32
    2. Stores all sensor readings in SQLite
    3. Every N seconds, runs an AI decision model
    4. Publishes commands back to ESP32 via MQTT
    5. Exposes a simple REST API for Node-RED / dashboards

  Install deps:
    pip install paho-mqtt flask scikit-learn pandas numpy joblib
============================================================
"""

import json
import time
import sqlite3
import threading
import logging
from datetime import datetime, timedelta
from collections import deque

import numpy as np
import pandas as pd
import paho.mqtt.client as mqtt
from flask import Flask, jsonify, request
from sklearn.ensemble import IsolationForest
from sklearn.preprocessing import StandardScaler
import joblib
import os

# ── CONFIG ────────────────────────────────────────────────────
MQTT_BROKER   = "localhost"       # broker runs on same machine
MQTT_PORT     = 1883
AI_INTERVAL   = 10               # run AI decision every N seconds
DB_PATH       = "iot_data.db"
MODEL_PATH    = "ai_model.pkl"
SCALER_PATH   = "ai_scaler.pkl"
MIN_SAMPLES   = 20               # minimum readings before AI kicks in
LOG_WINDOW    = 200              # how many recent readings to keep in memory

# Thresholds for rule-based decisions (AI layer 1)
RULE_ANALOG_HIGH  = 700
RULE_ANALOG_LOW   = 50
RULE_TEMP_HIGH    = 60.0
RULE_TEMP_LOW     = -10.0

# ── LOGGING ───────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.FileHandler("hub.log"),
        logging.StreamHandler()
    ]
)
log = logging.getLogger("AIHub")

# ── STATE ─────────────────────────────────────────────────────
recent_readings   = deque(maxlen=LOG_WINDOW)   # fast in-memory window
device_states     = {}                          # last known state per device
ai_model          = None
ai_scaler         = None
last_ai_run       = 0

# ── DATABASE ─────────────────────────────────────────────────
def init_db():
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS telemetry (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            ts        TEXT NOT NULL,
            device    TEXT NOT NULL,
            analog    REAL,
            digital   INTEGER,
            temp_c    REAL,
            relay     INTEGER,
            rssi      INTEGER
        )
    """)
    c.execute("""
        CREATE TABLE IF NOT EXISTS events (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            ts        TEXT NOT NULL,
            device    TEXT NOT NULL,
            event     TEXT,
            reason    TEXT,
            source    TEXT DEFAULT 'hub'
        )
    """)
    c.execute("""
        CREATE TABLE IF NOT EXISTS ai_decisions (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            ts        TEXT NOT NULL,
            device    TEXT NOT NULL,
            decision  TEXT,
            reason    TEXT,
            confidence REAL
        )
    """)
    conn.commit()
    conn.close()
    log.info("Database ready: %s", DB_PATH)

def db_insert_telemetry(data: dict):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
        INSERT INTO telemetry (ts, device, analog, digital, temp_c, relay, rssi)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    """, (
        datetime.utcnow().isoformat(),
        data.get("device", "unknown"),
        data.get("analog"),
        int(data.get("digital", False)),
        data.get("temp_c"),
        int(data.get("relay", False)),
        data.get("rssi")
    ))
    conn.commit()
    conn.close()

def db_log_event(device: str, event: str, reason: str = "", source: str = "hub"):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
        INSERT INTO events (ts, device, event, reason, source) VALUES (?, ?, ?, ?, ?)
    """, (datetime.utcnow().isoformat(), device, event, reason, source))
    conn.commit()
    conn.close()

def db_log_decision(device: str, decision: str, reason: str, confidence: float):
    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
        INSERT INTO ai_decisions (ts, device, decision, reason, confidence)
        VALUES (?, ?, ?, ?, ?)
    """, (datetime.utcnow().isoformat(), device, decision, reason, confidence))
    conn.commit()
    conn.close()

def db_get_recent(device: str, minutes: int = 60) -> pd.DataFrame:
    conn = sqlite3.connect(DB_PATH)
    cutoff = (datetime.utcnow() - timedelta(minutes=minutes)).isoformat()
    df = pd.read_sql_query("""
        SELECT * FROM telemetry
        WHERE device = ? AND ts > ?
        ORDER BY ts DESC
    """, conn, params=(device, cutoff))
    conn.close()
    return df

# ── AI MODEL ─────────────────────────────────────────────────
def load_or_create_model():
    """Load existing model or create a fresh one."""
    global ai_model, ai_scaler
    if os.path.exists(MODEL_PATH) and os.path.exists(SCALER_PATH):
        ai_model  = joblib.load(MODEL_PATH)
        ai_scaler = joblib.load(SCALER_PATH)
        log.info("Loaded existing AI model from disk.")
    else:
        ai_model  = IsolationForest(n_estimators=100, contamination=0.05, random_state=42)
        ai_scaler = StandardScaler()
        log.info("Created new IsolationForest anomaly detection model.")

def train_model(device: str):
    """Retrain the AI model on all stored data for this device."""
    global ai_model, ai_scaler
    df = db_get_recent(device, minutes=1440)  # last 24 hours
    if len(df) < MIN_SAMPLES:
        log.info("Not enough data to train (%d samples, need %d)", len(df), MIN_SAMPLES)
        return False

    features = df[["analog", "temp_c", "relay"]].dropna().values
    ai_scaler = StandardScaler()
    X = ai_scaler.fit_transform(features)
    ai_model.fit(X)

    joblib.dump(ai_model, MODEL_PATH)
    joblib.dump(ai_scaler, SCALER_PATH)
    log.info("AI model retrained on %d samples.", len(features))
    return True

def predict_anomaly(analog: float, temp_c: float, relay: int) -> tuple[bool, float]:
    """Returns (is_anomaly, confidence_score)."""
    if ai_model is None or ai_scaler is None:
        return False, 0.0
    try:
        X = ai_scaler.transform([[analog, temp_c, relay]])
        score = ai_model.score_samples(X)[0]   # more negative = more anomalous
        is_anomaly = ai_model.predict(X)[0] == -1
        # Normalize to 0–1 confidence
        confidence = max(0.0, min(1.0, (-score + 0.5) / 1.0))
        return is_anomaly, round(confidence, 3)
    except Exception as e:
        log.warning("Anomaly prediction error: %s", e)
        return False, 0.0

# ── AI DECISION ENGINE ────────────────────────────────────────
def make_decision(device: str, data: dict) -> dict | None:
    """
    Multi-layer decision engine:
      Layer 1 — Hard rules  (deterministic, always active)
      Layer 2 — Anomaly AI  (IsolationForest on recent history)
      Layer 3 — Trend logic (rolling average, rate of change)

    Returns a command dict or None if no action needed.
    """
    analog  = data.get("analog", 0)
    temp_c  = data.get("temp_c", 25)
    relay   = int(data.get("relay", False))
    digital = data.get("digital", False)

    # ── Layer 1: Hard rules ──
    if analog > RULE_ANALOG_HIGH:
        return _cmd(device, "relay_off", "rule_analog_critical", confidence=1.0)

    if temp_c > RULE_TEMP_HIGH:
        return _cmd(device, "relay_off", "rule_overheat", confidence=1.0)

    if temp_c < RULE_TEMP_LOW:
        return _cmd(device, "relay_off", "rule_undercold", confidence=1.0)

    # ── Layer 2: Anomaly detection ──
    is_anomaly, confidence = predict_anomaly(analog, temp_c, relay)
    if is_anomaly and confidence > 0.7:
        log.warning("[AI] Anomaly detected for %s (confidence=%.2f)", device, confidence)
        return _cmd(device, "relay_off", f"ai_anomaly_detected", confidence=confidence)

    # ── Layer 3: Trend analysis ──
    trend_decision = analyze_trend(device, analog, temp_c)
    if trend_decision:
        return trend_decision

    return None   # no action needed

def analyze_trend(device: str, current_analog: float, current_temp: float) -> dict | None:
    """Look at rolling stats over the recent window to catch gradual drift."""
    if len(recent_readings) < 10:
        return None

    window = [r for r in recent_readings if r.get("device") == device][-20:]
    if len(window) < 10:
        return None

    analogs = [r.get("analog", 0) for r in window]
    temps   = [r.get("temp_c", 25) for r in window]

    analog_mean  = np.mean(analogs)
    analog_std   = np.std(analogs)
    temp_slope   = np.polyfit(range(len(temps)), temps, 1)[0]  # degrees/reading

    # Spike detection: current reading > mean + 3*std
    if analog_std > 0 and current_analog > analog_mean + 3 * analog_std:
        confidence = min(1.0, (current_analog - analog_mean) / (analog_std * 3 + 1e-6))
        return _cmd(device, "relay_off", f"trend_spike_analog", confidence=round(confidence, 2))

    # Rising temperature trend: > 2 degrees/reading sustained
    if temp_slope > 2.0:
        return _cmd(device, "relay_off", "trend_temp_rising_fast", confidence=0.75)

    # Everything looks normal and relay is off — maybe turn on?
    # (Customize this for your use case)
    if current_analog < RULE_ANALOG_LOW and not any(r.get("relay") for r in window[-5:]):
        return _cmd(device, "relay_on", "trend_values_normal", confidence=0.6, duration_ms=10000)

    return None

def _cmd(device, action, reason, confidence=1.0, duration_ms=0) -> dict:
    return {
        "device":      device,
        "action":      action,
        "reason":      reason,
        "confidence":  confidence,
        "duration_ms": duration_ms,
        "ts":          datetime.utcnow().isoformat()
    }

# ── MQTT ─────────────────────────────────────────────────────
mqtt_client = mqtt.Client(client_id="ai_hub")

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        log.info("MQTT connected to broker at %s:%d", MQTT_BROKER, MQTT_PORT)
        client.subscribe("iot/telemetry")
        client.subscribe("iot/log")
        client.subscribe("iot/status")
    else:
        log.error("MQTT connection failed, rc=%d", rc)

def on_message(client, userdata, msg):
    global last_ai_run
    try:
        payload = json.loads(msg.payload.decode())
        topic   = msg.topic

        if topic == "iot/telemetry":
            handle_telemetry(payload)

        elif topic == "iot/log":
            device = payload.get("device", "unknown")
            event  = payload.get("event", "unknown")
            db_log_event(device, event, source="esp32")
            log.info("[LOG] %s → %s", device, event)

        elif topic == "iot/status":
            device = payload.get("device", "unknown")
            device_states[device] = {"last_seen": time.time(), "uptime": payload.get("uptime")}
            log.debug("[STATUS] %s alive (uptime=%ss)", device, payload.get("uptime"))

    except Exception as e:
        log.error("Message handling error: %s", e)

def handle_telemetry(data: dict):
    global last_ai_run
    device = data.get("device", "unknown")

    # Store in memory window
    recent_readings.append(data)

    # Persist to database
    db_insert_telemetry(data)

    # Update device state
    device_states[device] = {**data, "last_seen": time.time()}

    # Run AI decision engine (throttled)
    if time.time() - last_ai_run >= AI_INTERVAL:
        last_ai_run = time.time()
        threading.Thread(target=run_ai_for_device, args=(device, data), daemon=True).start()

def run_ai_for_device(device: str, data: dict):
    try:
        decision = make_decision(device, data)
        if decision:
            command_json = json.dumps(decision)
            mqtt_client.publish("iot/command", command_json)
            db_log_decision(device, decision["action"], decision["reason"], decision["confidence"])
            log.info("[AI→ESP32] %s: %s (reason=%s conf=%.2f)",
                     device, decision["action"], decision["reason"], decision["confidence"])
    except Exception as e:
        log.error("AI decision error: %s", e)

def publish_manual_command(device: str, action: str, reason: str = "manual", duration_ms: int = 0):
    cmd = _cmd(device, action, reason, duration_ms=duration_ms)
    mqtt_client.publish("iot/command", json.dumps(cmd))
    log.info("[MANUAL→ESP32] %s: %s", device, action)

# ── REST API (for Node-RED and dashboards) ───────────────────
app = Flask(__name__)

@app.route("/api/status")
def api_status():
    return jsonify({
        "devices": {
            k: {**v, "online": (time.time() - v.get("last_seen", 0)) < 60}
            for k, v in device_states.items()
        },
        "model_loaded": ai_model is not None,
        "recent_readings": len(recent_readings)
    })

@app.route("/api/history/<device>")
def api_history(device):
    minutes = int(request.args.get("minutes", 60))
    df = db_get_recent(device, minutes)
    return jsonify(df.to_dict(orient="records"))

@app.route("/api/command", methods=["POST"])
def api_command():
    """Node-RED calls this to send manual commands."""
    body   = request.get_json()
    device = body.get("device", "all")
    action = body.get("action")
    reason = body.get("reason", "api_manual")
    dur    = int(body.get("duration_ms", 0))
    if not action:
        return jsonify({"error": "action required"}), 400
    publish_manual_command(device, action, reason, dur)
    return jsonify({"status": "sent", "device": device, "action": action})

@app.route("/api/train/<device>", methods=["POST"])
def api_train(device):
    """Manually trigger model retraining."""
    success = train_model(device)
    return jsonify({"trained": success})

@app.route("/api/decisions/<device>")
def api_decisions(device):
    conn = sqlite3.connect(DB_PATH)
    df = pd.read_sql_query("""
        SELECT * FROM ai_decisions WHERE device=? ORDER BY ts DESC LIMIT 50
    """, conn, params=(device,))
    conn.close()
    return jsonify(df.to_dict(orient="records"))

# ── BACKGROUND: periodic model retraining ───────────────────
def retraining_loop():
    """Retrain AI model every hour in the background."""
    while True:
        time.sleep(3600)
        for device in list(device_states.keys()):
            log.info("[TRAIN] Retraining model for %s...", device)
            train_model(device)

# ── ENTRY POINT ──────────────────────────────────────────────
if __name__ == "__main__":
    init_db()
    load_or_create_model()

    # MQTT
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    mqtt_client.loop_start()

    # Background retraining
    threading.Thread(target=retraining_loop, daemon=True).start()

    # REST API
    log.info("Starting REST API on http://0.0.0.0:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)