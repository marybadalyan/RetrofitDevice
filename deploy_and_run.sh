#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PIO="/Users/mbadalyan/.platformio/penv/bin/pio"
PYTHON="/opt/homebrew/Cellar/python@3.14/3.14.0_1/Frameworks/Python.framework/Versions/3.14/Resources/Python.app/Contents/MacOS/Python"
PREFS="$SCRIPT_DIR/prefferences.h"

# ── Detect local IP and patch prefferences.h ────────────────────────────────

IP=$(ipconfig getifaddr en0)
if [ -z "$IP" ]; then
    echo "ERROR: could not detect IP on en0. Is Wi-Fi connected?"
    exit 1
fi
echo "==> Detected IP: $IP"

# Replace IP in SERVER_URL and kHubHost, leave everything else untouched
sed -i '' "s|SERVER_URL  \"http://[0-9.]*:|SERVER_URL  \"http://$IP:|" "$PREFS"
sed -i '' "s|kHubHost = \"[0-9.]*\"|kHubHost = \"$IP\"|"              "$PREFS"

echo "==> prefferences.h patched."
echo ""

# ── Detect serial ports and patch platformio.ini ─────────────────────────────

# Sort available ports alphabetically: usbserial-0001 sorts before usbserial-3
# which matches the current convention: thermoDevice=0001, heater=3
AVAILABLE_PORTS=($(ls /dev/tty.usbserial-* 2>/dev/null | sort))
PORT_COUNT=${#AVAILABLE_PORTS[@]}

if [ "$PORT_COUNT" -eq 0 ]; then
    echo "ERROR: No USB serial devices found. Connect both devices and retry."
    exit 1
elif [ "$PORT_COUNT" -lt 2 ]; then
    echo "WARNING: Only 1 USB serial device found (${AVAILABLE_PORTS[0]}) — expected 2."
    THERMO_PORT="${AVAILABLE_PORTS[0]}"
    HEATER_PORT="${AVAILABLE_PORTS[0]}"
else
    THERMO_PORT="${AVAILABLE_PORTS[0]}"   # alphabetically first  → thermoDevice
    HEATER_PORT="${AVAILABLE_PORTS[1]}"   # alphabetically second → heater
    [ "$PORT_COUNT" -gt 2 ] && \
        echo "WARNING: ${PORT_COUNT} serial ports found — using $THERMO_PORT (thermoDevice) and $HEATER_PORT (heater)."
fi

echo ""
echo "  thermoDevice → $THERMO_PORT"
echo "  heater       → $HEATER_PORT"
echo ""
echo "Port assignment is based on the dongle's USB serial number (alphabetical sort)."
echo "If the dongles were swapped between devices, type 's' to swap, or Enter to continue."
read -r CONFIRM
if [ "$CONFIRM" = "s" ] || [ "$CONFIRM" = "S" ]; then
    TMP="$THERMO_PORT"; THERMO_PORT="$HEATER_PORT"; HEATER_PORT="$TMP"
    echo "==> Swapped:  thermoDevice → $THERMO_PORT   heater → $HEATER_PORT"
fi
echo ""

# ── Firmware uploads ────────────────────────────────────────────────────────

echo "==> [1/2] Uploading heater firmware  (port: $HEATER_PORT)..."
"$PIO" run -t upload -e heater --upload-port "$HEATER_PORT"
HEATER_STATUS=$?
[ $HEATER_STATUS -ne 0 ] && echo "WARNING: heater upload failed (exit $HEATER_STATUS) — monitor will still open."

echo ""
echo "==> [2/2] Uploading thermoDevice firmware  (port: $THERMO_PORT)..."
"$PIO" run -t upload -e thermoDevice --upload-port "$THERMO_PORT"
THERMO_STATUS=$?
[ $THERMO_STATUS -ne 0 ] && echo "WARNING: thermoDevice upload failed (exit $THERMO_STATUS) — monitor will still open."

echo ""

# ── Serial monitors in new Terminal windows ──────────────────────────────────

echo "==> Opening heater serial monitor..."
osascript <<APPLESCRIPT
tell application "Terminal"
    do script "printf '\\033]0;Heater Monitor\\007' && $PIO device monitor --port $HEATER_PORT --baud 115200"
    activate
end tell
APPLESCRIPT

sleep 0.5

echo "==> Opening thermoDevice serial monitor..."
osascript <<APPLESCRIPT
tell application "Terminal"
    do script "printf '\\033]0;ThermoDevice Monitor\\007' && $PIO device monitor --port $THERMO_PORT --baud 115200"
    activate
end tell
APPLESCRIPT

sleep 0.5

# ── Hub server (runs in this terminal) ───────────────────────────────────────

echo ""
echo "==> Starting hub server at http://localhost:5000 ..."
echo ""

cd "$SCRIPT_DIR"
PYTHONPATH="$SCRIPT_DIR/.pkgs-hub" \
"$PYTHON" -m uvicorn thermohub:app --host 0.0.0.0 --port 5000
