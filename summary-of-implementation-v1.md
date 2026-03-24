# MURAL Firmware — Implementation Summary v1

## Overview

The MURAL wall plotter (ESP32 + 2× NEMA 17 steppers, TMC2209 drivers, GT2 pulleys, MG90s servo, SSD1306 OLED) uses a 6-phase state machine to guide the user from belt retraction to drawing. This document summarises all bugs found and fixes applied during the first debugging and implementation session.

---

## Problems Found and Fixed

### 1. Phase Transition Bugs (Firmware)

The intended phase order is:

```
[0] RetractBelts → [1] SetTopDistance → [2] ExtendToHome → [3] PenCalibration → [4] SvgSelect → [5] BeginDrawing
```

All four firmware-side transitions were wrong:

| File | Was transitioning to | Should transition to |
|---|---|---|
| `retractbeltsphase.cpp` | `ExtendToHome` | `SetTopDistance` |
| `settopdistancephase.cpp` | `SvgSelect` | `ExtendToHome` |
| `pencalibrationphase.cpp` | `BeginDrawing` | `SvgSelect` |
| `svgselectphase.cpp` | `RetractBelts` | `BeginDrawing` |
| `phasemanager.cpp` — `reset()` | `SetTopDistance` | `RetractBelts` |

**Effect:** The device skipped `SetTopDistance` entirely, so `topDistance` stayed at `-1`. When `ExtendToHome` was reached, `beginLinearTravel()` threw `std::invalid_argument("not ready")` and the ESP32 crashed before the motors moved. The visible symptom was belts extending only ~4 cm (the hardware retraction stop) after a reboot.

**Fix:** Corrected all five transition targets in the respective `.cpp` files.

---

### 2. Race Condition in `client.js`

**File:** `data/www/client.js`

`postCommand()` was missing a `return` statement:

```javascript
// Before
async function postCommand(command) {
    $.post("/command", {command}).fail(...);
}

// After
async function postCommand(command) {
    return $.post("/command", {command}).fail(...);
}
```

**Effect:** Callers that `await`ed `postCommand()` resolved immediately (the Promise resolved to `undefined`). Stop-motor commands (`l-0`, `r-0`) fired as fire-and-forget and could arrive at the ESP32 after the phase had already transitioned, causing the server to return HTTP 400 (handled by `NotSupportedPhase`), which triggered the `alert("Command failed")` modal. This was intermittent because it depended on network timing.

**Fix:** Added `return` before `$.post(...)`.

---

### 3. Wrong Microstepping Constant (stale binary)

**File:** `src/movement.h`

The source code correctly defined:
```cpp
constexpr int stepsPerRotation = 200 * 16; // 1/16 microstepping = 3200 steps/rev
```

However, the firmware binary that had been flashed was compiled from an older version of the file that used `200 * 8` (1/8 microstepping = 1600 steps/rev). This caused all belt length calculations to command exactly half the required motor travel.

**Effect:** Belts extended to ~40 cm instead of the correct ~88 cm needed to hang the plotter on 1285 mm wall anchors.

**Fix:** Rebuilt the firmware from the current source. No code change was needed — the constant was already correct, the binary just needed to be regenerated.

---

### 4. Missing Worker.js in LittleFS

**Directory:** `data/www/worker/`

The TypeScript drawing worker (`tsc/src/`) is compiled by webpack into `tsc/dist_packed/main.js` and then copied to `data/www/worker/worker.js` by `build.py` as a PlatformIO pre-build step. However, `build.py` only runs during `pio run` (firmware build), not during `pio run --target buildfs` (filesystem build). When the filesystem was rebuilt independently, the `worker/` directory was empty.

**Effect:** The browser loaded the SvgSelect page and the worker failed to start (404), causing the drawing preview to hang indefinitely on the animated progress bar with no error shown.

**Fix:** Ran webpack manually in `tsc/`, then copied the output:
```bash
cd tsc && npx webpack --mode=production --node-env=production
cp tsc/dist_packed/main.js data/www/worker/worker.js
```
The flasher's `serve.py` `/build` route now triggers the full build chain that includes this step.

---

## Features Added

### 5. Browser-Visible Log System

**Files:** `src/main.cpp`, `src/movement.cpp`, `src/phases/*.cpp`, `src/runner.cpp`

Added a runtime log buffer accessible from the web UI, to allow diagnosis without a serial connection.

**Firmware side (`main.cpp`):**
```cpp
std::vector<String> logBuffer;
const int LOG_BUFFER_MAX = 50;

void addLog(const String& msg) {
    Serial.println(msg);
    if ((int)logBuffer.size() >= LOG_BUFFER_MAX)
        logBuffer.erase(logBuffer.begin());
    logBuffer.push_back(msg);
}
```

Two HTTP endpoints added:
- `GET /log` — returns all buffered log lines as plain text
- `POST /clearLog` — clears the buffer

`addLog()` is called (via `extern` declaration) from all phase files, `movement.cpp`, and `runner.cpp`, covering the full lifecycle:

```
Phase -> RetractBelts
RetractBelts: user confirmed belts retracted
Phase -> SetTopDistance
SetTopDistance: param='1285' parsed=1285
Phase -> ExtendToHome
ExtendToHome: starting belt extension
extendToHome: topDistance=1285 width=771.00 homeX=385.50 homeY=350.00 originSteps=3209
beginLinearTravel: x=385.50 y=350.00 leftSteps=68572 rightSteps=68572 ...
ExtendToHome: motion complete, belts at home position
Phase -> PenCalibration
PenCalibration: pen distance set, angle=90
PenCalibration: complete, pen raised
Phase -> SvgSelect
SvgSelect: upload started, size=...B free=...B
SvgSelect: upload complete, ...B written
Phase -> BeginDrawing
BeginDrawing: starting plot job
Runner: commands loaded, totalDistance=...mm
BeginDrawing: runner started, web server shutting down
Runner: progress=10%
...
Runner: drawing complete, returning to home then restarting
```

**Web UI side (`data/www/`):**
- Floating "Logs" button (bottom-right corner) visible on all screens
- Modal with scrollable log view, Refresh, and Clear buttons
- `fetchLogs()` polls `GET /log` on open and refresh

---

### 6. Local Firmware Flasher

**Files:** `flasher/serve.py`, `flasher/flasher.html`

A self-contained local web flasher for the modified firmware, equivalent in function to `getmural.me/firmware_flasher.html`.

**`flasher/serve.py`** — Python stdlib HTTP server (no external dependencies beyond PlatformIO):
- `GET /` — serves `flasher.html`
- `GET /status` — returns JSON with existence of all 5 binary files
- `GET /firmware/<name>` — serves binary files from `.pio/build/esp32dev/`
- `POST /build` — runs `pio run` then `pio run --target buildfs`
- Auto-opens browser after startup

**`flasher/flasher.html`** — 4-step browser UI using esptool-js v0.4.3 (Web Serial API, Chrome/Edge only):
1. **Build** — shows binary status, triggers build via `/build`
2. **Connect** — Web Serial device picker (hold BOOT button instruction)
3. **Flash** — fetches all 5 binaries and writes at correct addresses
4. **Console** — live log of all operations

Flash layout (from `partitions.csv`):

| Binary | Address |
|---|---|
| `bootloader.bin` | `0x1000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xE000` |
| `firmware.bin` | `0x10000` |
| `littlefs.bin` | `0x13C000` |

Key implementation detail: `flashSize` must be set to `"keep"` (not `"detect"`) in the esptool-js `writeFlash` call. The `flashSizeBytes("detect")` helper returns `-1`, causing every binary to fail the bounds check immediately.

---

## Running the Flasher

```bash
conda run -n MURAL python3 flasher/serve.py --port 8090
```

Opens at [http://localhost:8090](http://localhost:8090). Click **Build now**, then connect the ESP32 (hold BOOT), then **Flash**.

---

## Full Phase Flow (Corrected)

```
Power on
  └─ WiFiManager: connect to saved WiFi or show "Mural" AP for setup
       └─ Web server starts at http://mural.local

[0] RetractBelts
     User manually retracts belts until they stall
     → presses "Belts retracted" button

[1] SetTopDistance
     User measures and enters distance between wall pins (mm)
     → presses "Set distance"

[2] ExtendToHome
     Motors extend belts to computed home position (~88 cm for 1285 mm pins)
     → automatic transition when motors stop

[3] PenCalibration
     User adjusts pen height with slider until pen tip just touches wall
     → presses "Set pen distance"

[4] SvgSelect
     User uploads SVG, browser worker converts to movement commands
     User previews and adjusts infill density / flatten paths
     → presses "Accept" → commands uploaded to ESP32 LittleFS

[5] BeginDrawing
     User presses "Begin Drawing"
     Web server shuts down, ESP32 focuses on motor control
     Progress shown on OLED display
     → on completion: returns to home, then ESP.restart()
```
