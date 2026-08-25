---
publishDate: 2026-08-25
title: Persona - Predictive Parkinson Motor Response Quantification & Medication Timing Optimization Platform
excerpt: A wearable ESP32 device that tracks tremor and muscle activity to monitor Parkinson's motor symptoms, reminds patients to take medication, and measures how their tremor responds after each dose.
image: docs/image.jpg
tags:
- ESP32
- Healthcare
- Parkinsons
---

> A wrist-worn motor-symptom tracker that reminds patients to take their medication and measures how well it's working.

---

## Acknowledgements

Built on the MYOSA motherboard platform (MakeSense Community). Thanks to the MYOSA organizers and community for the hardware base and support.

---

## Overview

Persona is a wearable prototype that continuously monitors motor symptoms associated with Parkinson's disease and ties that monitoring directly to medication timing.

The device sits on the wrist and reads:
- **Tremor** - via an accelerometer, looking at how sharply acceleration changes moment to moment
- **Muscle activation** - via a surface ECG sensor
- **Whether it's actually being worn** - via a proximity sensor, so readings taken with the device off-wrist aren't mistaken for real data

On a fixed schedule, Persona reminds the wearer to take their medication (OLED display + buzzer), and the wearer confirms or postpones the dose with two physical buttons. When a dose is confirmed, Persona captures a tremor "baseline" at that moment and then tracks the *best* (lowest-tremor) reading over the following window - giving a rough picture of how much the medication improved motor symptoms, and eventually whether that improvement is fading ("wearing-off").

All of this is also exposed live over a self-hosted WiFi dashboard, so a caregiver or clinician can view the same numbers from a phone or laptop without any extra app.

**Key features:**
* Real-time tremor magnitude and EMG activation tracking
* Wear-state detection (proximity sensor) so off-wrist noise isn't recorded as symptom data
* Medication reminders with Confirm / Postpone buttons and buzzer + OLED feedback
* Automatic pre/post-dose tremor comparison to gauge medication response
* Prototype "wearing-off" detection heuristic
* Live WiFi dashboard (HTML) and JSON API, no companion app required

**Who it's for:** People with Parkinson's disease (or their caregivers) who want a simple way to track whether medication timing is actually keeping symptoms under control, and researchers/hobbyists exploring low-cost wearable motor-symptom monitoring.

**What problem it solves:** Parkinson's medication effectiveness often fades between doses ("wearing-off"), but patients and caregivers rarely have objective data on when or how much - they're relying on how the patient *feels*. Persona gives a simple, continuously logged, objective signal (tremor + EMG) tied to the exact moment each dose was taken.

> **Note:** The Motor Performance Index (MPI) score used in this project is a prototype heuristic (60% tremor / 40% EMG, both simple threshold-based formulas) and is **not a clinically validated diagnostic score**. It's intended as a relative, personal tracking signal - not a medical measurement.

---

## Demo / Examples

### Images

<p align="center">
  <img src="/docs/image.jpg" width="800"><br/>
  <i>MYOSA sensor boards (OLED, BMP180, APDS9960) wired to the ESP32 main board</i>
</p>

### Videos

<a href="./docs/video.mp4"> ▶ Play video</a>

---

## Features (Detailed)

### 1. Motion & Muscle Sensing
An MPU6050 accelerometer samples motion at ~50 Hz. Tremor magnitude is calculated as the change in acceleration between consecutive fast-loop samples, which better captures the 4-6 Hz frequency range typical of Parkinsonian tremor than a slower sampling rate would. A MyoWare 2.0 EMG sensor is read on the same fast loop, rectified, and smoothed with an exponential moving average to produce a muscle-activation envelope.

### 2. Wear Detection
An APDS9960 proximity sensor checks whether the device is actually against skin. This gates medication-response tracking: if the device is removed mid-tracking, baseline/best-tremor capture is paused rather than recording meaningless noise as real data.

### 3. Medication Reminder & Response Tracking
On a timer, the device enters a "Reminding" state - OLED + buzzer alert the wearer, who presses Confirm or Postpone. Confirming captures a tremor baseline at that instant and starts tracking the best (lowest) tremor reading over the following period, giving a rough before/after picture of the dose's effect. A simple heuristic later flags a possible "wearing-off" event if tremor climbs back up well after the best response was recorded.

### 4. Live Dashboard
The ESP32 hosts its own WiFi access point and a small web server, serving both a human-readable dashboard (auto-refreshing) and a `/data` JSON endpoint for anyone who wants to log or graph the readings externally.

---

## Usage Instructions

1. Wire the sensors as described in **Requirements / Installation** below, then flash the firmware to the ESP32.
2. On boot, the OLED shows sensor init status and the device connects as a WiFi access point.
3. Connect a phone or laptop to the WiFi network printed on Serial / shown on the OLED.
4. Open the dashboard in a browser at the ESP32's IP address (also printed on Serial) to view live MPI, tremor, EMG, wear status, and medication state.
5. When the buzzer sounds for a medication reminder, press **Confirm** once the dose is taken, or **Postpone** to be reminded again shortly after.

Flashing the firmware (Arduino CLI example):

```plaintext
arduino-cli compile --fqbn esp32:esp32:esp32wroom persona
arduino-cli upload -p <PORT> --fqbn esp32:esp32:esp32wroom persona
```

Reading live data programmatically:

```python
import requests

data = requests.get("http://<esp32-ip>/data").json()
print(data["mpi"], data["tremor"], data["medState"])
```

---

## Tech Stack

* **ESP32-WROOM-32E** (MYOSA motherboard) - main controller, WiFi
* **Arduino / C++** - firmware
* **Adafruit_MPU6050** - accelerometer/tremor sensing
* **SparkFun_APDS9960_RGB_and_Gesture_Sensor** - wear detection
* **Adafruit_BMP085_Unified** - environmental pressure sensing
* **Adafruit_SSD1306 + Adafruit_GFX** - OLED status display
* **ESP32 WebServer library** - onboard WiFi dashboard + JSON API

---

## Requirements / Installation

**Hardware:**
* MYOSA motherboard with ESP32-WROOM-32E
* MPU6050 (accelerometer/gyro)
* APDS9960 (proximity/gesture)
* BMP180 (barometric pressure)
* 0.96" SSD1306 OLED display
* MyoWare 2.0 EMG sensor
* 2x tactile pushbuttons (Confirm / Postpone)
* Active buzzer module (SIG / 5V / GND)

**Wiring summary:**

| Signal | GPIO |
|---|---|
| I2C SDA | 21 |
| I2C SCL | 22 |
| EMG signal | 34 |
| Confirm button | 25 |
| Postpone button | 26 |
| Buzzer SIG | 27 |
| Status LED | 2 |

All I2C sensors (MPU6050 @ 0x69, APDS9960 @ 0x39, BMP180 @ 0x77, SSD1306 @ 0x3C) share the same SDA/SCL bus.

**Arduino Library Dependencies:**

```bash
Adafruit BMP085 Library
Adafruit BMP085 Unified
Adafruit BusIO
Adafruit GFX Library
Adafruit MPU6050
Adafruit SSD1306
Adafruit Unified Sensor
SparkFun APDS9960 RGB and Gesture Sensor
```

Install these via the Arduino Library Manager, then select an ESP32 board profile before compiling.

---

## File Structure

```
/persona
├─ persona.ino
├─ docs/
|   ├─ image.jpg
|   ├─ video.mp4
└─ README.md
```

---

## License 

MIT License

---

## Contribution Notes 

Issues and improvement ideas (e.g. clinically-informed tremor scoring, FFT-based frequency analysis, RTC-based real medication scheduling) are welcome via pull request or issue on the project repository.
