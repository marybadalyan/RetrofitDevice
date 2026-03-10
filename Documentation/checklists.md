**Checklist & Notes**

- Verify model integration using Blynk automation, widget triggering, and logs
- Set up two Blynk templates: one for the learning mode, one for the NEC protocol

---

**Planned Extensions**

- Blynk remote control
- Self-learning protocol mode
- Feed automation and widget logs into a model at a later stage
- Retrofit controller enhancements: scheduled command display, anomaly alerts, etc.

---


**TODO**
-  Before connecting any hardware, investigate whether cross-platform testing is feasible  CI support would be a bonus.
- remove sheduling to get a minimal configuration in no need oof refactorment ot bypassings make mocks and tests avalable to those make mockers for the ir heater and test the retrofit and vice versa 

**Hardware & Testing**

Since scheduling will ultimately be handled by the hub, and the current implementation is a mock, we can validate the scheduling logic before touching any hardware or hub connectivity. If successful, we can either remove the onboard scheduling or keep it as a fallback (in this case, the device wouldn't display an auto-generated schedule, but the user could still set one manually).

Firstly we will just hook up a device and use the shedule to controll the heater with events as specified in Blynk 

Alternatively — if the AI model running on the hub is in place — the hub could generate and push the schedule to the retrofit controller automatically. This depends on the AI component being ready, so it's deferred for now.

---

**Hardware Tests (in order)**

1. Confirm the heater runs correctly — initially without the OLED, using terminal logs showing status (time, temperature)
2. Verify NTP is working reliably
3. Confirm the remote controller can control the heater without protocol issues
4. Connect the retrofit device to Blynk via Wi-Fi successfully
5. Configure the code to control widgets through pins — web first, then mobile