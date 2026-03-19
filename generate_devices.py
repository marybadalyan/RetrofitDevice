#!/usr/bin/env python3
# generate_devices.py
# Run this once to generate unique IDs and passwords for your devices.
# Output: devices.csv  ← keep this file safe, it's your device registry

import random
import string
import csv
import os
import sys

# ── Config ───────────────────────────────────────────────────
NUM_DEVICES   = 10      # change this to however many devices you're making
ID_LENGTH     = 6       # e.g. "A3F9K2"
PASS_LENGTH   = 10      # e.g. "xK9#mP2$qR"
OUTPUT_FILE   = "devices.csv"
# ─────────────────────────────────────────────────────────────

def gen_id(existing):
    chars = string.ascii_uppercase + string.digits
    while True:
        candidate = ''.join(random.choices(chars, k=ID_LENGTH))
        if candidate not in existing:
            return candidate

def gen_password():
    letters = string.ascii_letters
    digits  = string.digits
    symbols = "#$@!"
    all_chars = letters + digits + symbols
    while True:
        pwd = ''.join(random.choices(all_chars, k=PASS_LENGTH))
        # Ensure at least one of each type
        if (any(c in digits  for c in pwd) and
            any(c in symbols for c in pwd) and
            any(c.isupper()  for c in pwd) and
            any(c.islower()  for c in pwd)):
            return pwd

def main():
    # Don't overwrite existing file accidentally
    if os.path.exists(OUTPUT_FILE):
        print(f"\n[!] {OUTPUT_FILE} already exists.")
        ans = input("    Append new devices? (y/n): ").strip().lower()
        if ans != 'y':
            print("    Cancelled.")
            sys.exit(0)

    existing_ids = set()
    rows_before = 0

    # Load existing IDs if appending
    if os.path.exists(OUTPUT_FILE):
        with open(OUTPUT_FILE) as f:
            reader = csv.DictReader(f)
            for row in reader:
                existing_ids.add(row["id"])
                rows_before += 1

    new_devices = []
    for i in range(NUM_DEVICES):
        device_id = gen_id(existing_ids)
        existing_ids.add(device_id)
        password  = gen_password()
        name      = f"Device {device_id}"
        new_devices.append({
            "id":       device_id,
            "password": password,
            "name":     name
        })

    # Write to CSV
    file_exists = os.path.exists(OUTPUT_FILE)
    with open(OUTPUT_FILE, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["id", "password", "name"])
        if not file_exists or rows_before == 0:
            writer.writeheader()
        writer.writerows(new_devices)

    print(f"\n✓  Generated {NUM_DEVICES} devices → {OUTPUT_FILE}")
    print(f"   Total devices in registry: {rows_before + NUM_DEVICES}\n")
    print(f"{'ID':<10} {'PASSWORD':<14} {'NAME'}")
    print("─" * 45)
    for d in new_devices:
        print(f"{d['id']:<10} {d['password']:<14} {d['name']}")
    print()
    print("[!] Keep devices.csv safe — it's your device registry.")
    print("    Use the ID and password to flash each ESP32.\n")

if __name__ == "__main__":
    main()