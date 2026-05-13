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

# ── Step-by-step port assignment ─────────────────────────────────────────────

list_ports() { ls /dev/tty.usbserial-* 2>/dev/null | sort; }

# Blocks until a port appears that wasn't in $1 (newline-separated snapshot)
wait_for_new_port() {
    local snapshot="$1"
    while true; do
        local new
        new=$(comm -13 <(echo "$snapshot") <(list_ports))
        [ -n "$new" ] && { echo "$new"; return; }
        sleep 0.5
    done
}

# If any serial devices are already plugged in, ask the user to unplug them first
EXISTING=$(list_ports)
if [ -n "$EXISTING" ]; then
    echo "The following serial devices are already connected:"
    echo "$EXISTING" | sed 's/^/    /'
    echo ""
    echo "Please unplug all devices, then press Enter..."
    read -r
    while [ -n "$(list_ports)" ]; do
        echo "  Still detecting devices — unplug all and press Enter."
        read -r
    done
    echo ""
fi

echo "==> [1/2]  Plug in the thermoDevice (temp sensor) now..."
SNAPSHOT=$(list_ports)
THERMO_PORT=$(wait_for_new_port "$SNAPSHOT")
echo "    Detected: $THERMO_PORT  →  thermoDevice"
echo ""

echo "==> [2/2]  Plug in the heater (HVAC) now..."
SNAPSHOT=$(list_ports)
HEATER_PORT=$(wait_for_new_port "$SNAPSHOT")
echo "    Detected: $HEATER_PORT  →  heater"
echo ""

echo "  thermoDevice → $THERMO_PORT"
echo "  heater       → $HEATER_PORT"
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
    set heaterWin to do script "$PIO device monitor --port $HEATER_PORT --baud 115200"
    set custom title of heaterWin to "Heater Monitor"
    activate
end tell
APPLESCRIPT

sleep 0.5

echo "==> Opening thermoDevice serial monitor..."
osascript <<APPLESCRIPT
tell application "Terminal"
    set thermoWin to do script "$PIO device monitor --port $THERMO_PORT --baud 115200"
    set custom title of thermoWin to "ThermoDevice Monitor"
    activate
end tell
APPLESCRIPT

sleep 0.5

echo "==> Opening ngrok tunnel (nontheoretic-alyce-noncommunistic.ngrok-free.dev → :5000)..."
osascript <<APPLESCRIPT
tell application "Terminal"
    set ngrokWin to do script "/opt/homebrew/bin/ngrok http --url=nontheoretic-alyce-noncommunistic.ngrok-free.dev 5000"
    set custom title of ngrokWin to "Web Tunnel (ngrok)"
    activate
end tell
APPLESCRIPT

sleep 0.5

# ── Cleanup: close monitor windows when this script exits ───────────────────

cleanup() {
    echo ""
    echo "==> Closing monitor terminals..."
    osascript <<'APPLESCRIPT'
    tell application "Terminal"
        set titles to {"Heater Monitor", "ThermoDevice Monitor", "Web Tunnel (ngrok)"}
        repeat with w in (get windows)
            if (custom title of w) is in titles then
                close w
            end if
        end repeat
    end tell
APPLESCRIPT
}
trap cleanup EXIT

# ── Hub server (runs in this terminal) ───────────────────────────────────────

echo ""
echo "==> Starting hub server at http://localhost:5000 ..."
echo ""

cd "$SCRIPT_DIR"
PYTHONPATH="$SCRIPT_DIR/.pkgs-hub" \
"$PYTHON" -m uvicorn thermohub:app --host 0.0.0.0 --port 5000
