# The Smart Window You Need

> *"Are you tired of standing up to open your window? Are you tired of **thinking** about standing up to open your window? Then, friend, have I got a solution for you."*

---

## What Is This, Exactly?

This is **The Smart Window You Need** — an IoT project built around an ESP32-S3 microcontroller and a humble 5mm LDR sensor, lovingly crafted by undergraduate students as part of a **Computer Networks** course.

The goal? Demonstrate the network capabilities of the ESP32 by connecting a real physical sensor to a WiFi-accessible REST API. Ask the device how bright it is. Ask it how open the window should be. Override it yourself. Calibrate it against your own specific flavour of sunlight. One day, a tiny servo will actually open the window for you. That day is coming. We can feel it.

The project is grounded, unpretentious, and entirely honest about what it is: a school project that does something real, explained without buzzwords (mostly).

---

## How It Works

An LDR (Light Dependent Resistor) sits somewhere near a window. The ESP32-S3 reads its voltage via ADC every `DELAY` milliseconds and feeds it into an exponential moving average (EMA) that smooths out brief flickers. When the smoothed reading drifts far enough from the last recorded level, the system recomputes the **window openness percentage** — a value between `0.0` (bright outside, window fully closed) and `1.0` (dark outside, window fully open).

All of this is exposed over WiFi via a simple HTTP/JSON API. Calibration values and the reaction time survive reboots — they are stored in the ESP32's NVS flash via the `Preferences` library.

A servo-based physical prototype is on the roadmap.

---

## Hardware

| Component | Detail |
|-----------|--------|
| Microcontroller | ESP32-S3 |
| Light Sensor | LDR 5mm |
| Connection | ADC pin `GPIO1`, 12-bit resolution, 11dB attenuation |

---

## Dependencies

Install these libraries via the Arduino Library Manager or PlatformIO before compiling:

| Library | Purpose |
|---------|---------|
| `ESPAsyncWebServer` | Non-blocking HTTP server (provides `AsyncJson.h` for `AsyncJsonResponse`) |
| `AsyncTCP` | Required by ESPAsyncWebServer on ESP32 |
| `ArduinoJson` | JSON serialization / deserialization |

`WiFi`, `Preferences`, and the ADC functions are part of the ESP32 Arduino core — no extra install needed.

---

## Getting Started

1. Open `main.ino` and fill in your network credentials at the top:
   ```cpp
   const char* WIFI_SSID = "YOUR_SSID";
   const char* WIFI_PASS = "YOUR_PASSWORD";
   ```
2. Flash the board. Open the Serial Monitor at **115200 baud**.
3. The device will print its IP address once connected:
   ```
   Connecting to WiFi....
   IP: 192.168.1.42
   Server started
   ```
4. Use that IP to hit the API from any device on the same network.

> On first boot, `MV_BRIGHT`, `MV_DARK`, and `REACTION_TIME` load from their compile-time defaults. Use the calibration endpoints to tune them to your physical setup — they will persist automatically.

---

## Configuration

### Compile-time constants

These live at the top of `main.ino` and require a reflash to change:

```cpp
constexpr uint32_t DELAY = 500;
```
How often the sensor is polled, in milliseconds.

---

```cpp
constexpr float LIGHT_DELTA = 50.0;
```
Minimum change in the smoothed voltage (mV) required to trigger a window percentage recomputation. Deadband against noise — raise it in flickery environments.

---

```cpp
constexpr uint32_t GRACE_PERIOD = 300;
```
How long (in seconds) a manual `/openness` override holds before the sensor resumes automatic control. Set to `0` to disable the hold entirely.

### Runtime-configurable (persisted to NVS)

These start from their defaults but can be updated at runtime via the API and survive reboots:

| Parameter | Default | API route |
|-----------|---------|-----------|
| `MV_BRIGHT` | `2200.0` mV | `POST /calibrate/bright` |
| `MV_DARK` | `2800.0` mV | `POST /calibrate/dark` |
| `REACTION_TIME` | `60.0` s | `POST /config/reaction_time` |

`SMOOTHING` (the EMA factor) is derived automatically from `REACTION_TIME` and `DELAY` — it is never set directly.

> **Calibration tip:** Leave the Serial Monitor open. Read `light_avg` in full sunlight — that is your `MV_BRIGHT`. Read it in complete darkness — that is your `MV_DARK`. Then hit the calibration endpoints in those conditions to save the values.

---

## API Reference

All responses are `application/json`. All POST endpoints that take a body expect `Content-Type: application/json`.

---

### `GET /sensor`

Returns the current smoothed LDR reading.

**Response**
```json
{ "mv": 2341.5 }
```

---

### `GET /window`

Returns the computed openness percentage and whether a manual override is active.

**Response**
```json
{ "perc": 0.34, "manual_override": false }
```

---

### `POST /openness`

Manually sets the window openness percentage. Suspends automatic sensor updates for `GRACE_PERIOD` seconds.

**Body**
```json
{ "perc": 0.75 }
```

**Response**
```json
{ "perc": 0.75, "grace_period_s": 300 }
```

---

### `POST /calibrate/bright`

Snapshots the current smoothed reading as the **"fully closed"** reference (`MV_BRIGHT`). Call this while the sensor is in full ambient light. Persists to NVS.

**Response**
```json
{ "mv_bright": 2198.3 }
```

---

### `POST /calibrate/dark`

Snapshots the current smoothed reading as the **"fully open"** reference (`MV_DARK`). Call this while the sensor is in complete darkness. Persists to NVS.

**Response**
```json
{ "mv_dark": 2791.7 }
```

---

### `POST /config/reaction_time`

Updates the EMA smoothing window at runtime. Higher values make the window less reactive to brief lighting changes; lower values make it snappier. Persists to NVS.

**Body**
```json
{ "seconds": 30.0 }
```

**Response**
```json
{ "reaction_time_s": 30.0, "smoothing": 59.0 }
```

---

## Serial Output

Everything is also streamed over Serial at `115200` baud for debugging:

```
last_light:2247.83
light_avg:2251.12
perc:0.34
grace:no
```

---

## Authors

Built with questionable sleep schedules by:

- [Fabio](https://github.com/Fabioomega)
- [Ian](https://github.com/ianandriani07)
- [Carol](https://github.com/carol-lanzu)

*Computer Networks — Undergraduate Course Project*
