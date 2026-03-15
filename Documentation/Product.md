Our product offers a smart solution for a generation of 'dumb' legacy units.

Legacy units can only be controlled physically — either through a remote controller (heaters, TVs, air coolers and conditioners) or by direct touch (coffee makers, radiators, water heaters, etc.). This means the user's physical presence is required, making it impossible to control these devices from more than roughly 200 meters away.

What if we introduced a device that could do it for us? That way, we could be in another city — or even another country — while our devices continue to function exactly as before.

---

**Demo**

For demonstration purposes, we will use a legacy IR-controlled heater operating on the NEC protocol. A typical legacy IR heater is controlled with a remote; we'll be using a simple 21-button remote working with the NEC protocol.

---

**Introducing the Retrofit Controller**

- Controls your unit remotely, regardless of your location
- Schedules commands autonomously
- Adjusts heating settings
- Collects operational data
- Generates reports, including anomaly detection (e.g. heater running for an extended period without a rise in temperature)
- Connects to a cloud platform where you can automate schedules or override them on demand
- Expandable to support a full home environment, integrating heaters, temperature controls, and more

Our goal is to make the physical remote controller obsolete — and replace it with something far better.

---

**Hardware**

The simulated legacy heater runs on an ESP32 microcontroller with an IR receiver and an OLED display showing the current temperature.

The retrofit controller is implemented on a separate ESP32 with an IR transmitter and a temperature sensor.

---

**Cloud & Automation — Blynk**

Blynk is an IoT platform that allows you to connect devices to the cloud via Wi-Fi provisioning. Each device is configured using a template consisting of a dashboard and datastreams, where datastreams link dashboard widgets to their corresponding hardware pins.

Blynk also supports automations, enabling us to leverage its built-in scheduling system while retaining logs for future monitoring and improvements.