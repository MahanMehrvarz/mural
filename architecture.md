# MURAL — System Architecture

MURAL is a wall-mounted drawing robot controlled by an ESP32 microcontroller. Two stepper motors spool timing belts that suspend the robot from two fixed anchor points on the wall. By coordinating belt lengths, the robot positions a servo-actuated pen anywhere on the wall surface. A browser-based interface handles image processing, calibration, and job execution.

---

## Hardware at a Glance

| Component | Part | Notes |
|---|---|---|
| MCU | NodeMCU ESP32 (WROOM-32) | WiFi + compute |
| Motors | 2× NEMA 17 stepper (200 steps/rev) | Pancake form factor |
| Drivers | 2× TMC2209 | 1/8 microstepping → 1600 steps/rev |
| Pulleys | GT2, 20-tooth, ⌀12.69 mm effective | Belt pitch 2 mm |
| Pen actuator | MG90s metal gear servo | GPIO 2, PWM |
| Display | Adafruit SSD1306 128×64 OLED | I2C 0x3C |
| Power | USB-C PD (12 V, 30 W) → LM2596 buck | Powers motors + MCU |

**GPIO pin map**

| Signal | Pin |
|---|---|
| Left STEP | 13 |
| Left DIR | 12 |
| Left EN | 14 |
| Right STEP | 27 |
| Right DIR | 26 |
| Right EN | 25 |
| Servo PWM | 2 |

---

## Physical Principle

The robot hangs from two anchor points (pulleys) fixed to the wall. Each motor spools one belt. The pen sits at the intersection of the two belt lengths — changing the ratio of left vs. right belt shifts the robot horizontally; paying out both equally lowers it.

Because the body is not perfectly rigid under gravity, it tilts slightly as it moves toward one side (like a pendulum). The firmware corrects for this by solving a torque equilibrium equation on every move.

---

## Software Architecture Overview

```
Browser (User)
│
├─ Web UI (HTML/JS served from LittleFS /www/)
│   └─ Web Worker (compiled TypeScript)
│       SVG/image → parse → optimize → command file
│
└─ HTTP POST /uploadCommands
        │
        ▼
   ESP32 Firmware (Arduino / PlatformIO)
   ┌─────────────────────────────────────────┐
   │  main.cpp        (setup + loop)          │
   │  PhaseManager    (state machine)         │
   │  AsyncWebServer  (HTTP endpoints)        │
   │  Runner          (reads /commands file)  │
   │  Tasks           (atomic operations)     │
   │  Movement        (kinematics + steppers) │
   │  Pen             (servo)                 │
   │  Display         (OLED)                  │
   └─────────────────────────────────────────┘
        │
        ▼
   TMC2209 drivers → NEMA 17 steppers → belts → pen position
   MG90s servo → pen up / pen down
```

---

## Firmware Subsystems

### 1. Main Loop (`src/main.cpp`)

Startup sequence (once):
1. Mount LittleFS filesystem
2. Initialise OLED display
3. Initialise stepper motors
4. Connect WiFi via WiFiManager (opens AP "Mural" if no saved network)
5. Start mDNS → accessible at `http://mural.local`
6. Initialise pen servo
7. Create Runner and PhaseManager
8. Register all HTTP endpoints
9. Start AsyncWebServer on port 80

Main loop (runs continuously):
```
loop():
  movement->runSteppers()           // pulse steppers — must be called every cycle
  runner->run()                     // execute one step of the current task
  phaseManager->currentPhase->loopPhase()  // phase-specific idle logic
```

The loop is non-blocking. No `delay()` calls exist in the critical path.

---

### 2. Phase Manager — State Machine (`src/phases/`)

The firmware progresses through six sequential phases. Each phase owns its HTTP endpoints; requests sent to the wrong phase return HTTP 400.

```
[0] RetractBelts
      ↓ user clicks "Done"
[1] SetTopDistance
      ↓ user enters pin distance
[2] ExtendToHome
      ↓ homing complete (automatic)
[3] PenCalibration
      ↓ user sets pen-down angle
[4] SvgSelect
      ↓ command file uploaded
[5] BeginDrawing
      ↓ drawing complete → ESP32 restarts
[0] RetractBelts  (next job)
```

---

### 3. Movement System (`src/movement.cpp`)

Central class. Owns both AccelStepper instances and all kinematic logic.

**Key methods:**

| Method | Purpose |
|---|---|
| `beginLinearTravel(x, y, speed)` | Compute belt lengths for target, command steppers |
| `getBeltLengths(x, y)` | Kinematic solver → returns (left_steps, right_steps) |
| `runSteppers()` | Stepper pulse generation — called every loop |
| `extendToHome()` | Move to home position (center-top of canvas) |
| `leftStepper(dir)` / `rightStepper(dir)` | Manual jog during setup |
| `extend1000mm()` | Calibration: extend belt 1000 mm to measure steps/mm |
| `setTopDistance(mm)` | Set anchor separation, compute safe drawing area |

**Speed constants:**
- Drawing: 500 steps/sec ≈ 12.5 mm/sec
- Rapid travel: 1500 steps/sec ≈ 37.5 mm/sec
- Resolution: 1600 steps/rev × 1/π × 1/12.69 mm ≈ **0.025 mm per step**

---

### 4. Kinematic Solver (`movement.cpp::getBeltLengths()`)

This is the most complex part of the firmware. It accounts for the robot tilting under gravity.

**Inputs:** target canvas coordinates (x, y) in mm
**Output:** (left_steps, right_steps) for the two motors

**Step-by-step:**

1. **Coordinate transform** — convert image-frame (x, y) to wall-frame by adding safety margins (sides reduced 20%, top reduced 20%).

2. **Iterative torque equilibrium** (up to 20 iterations):
   - Compute belt angles φ_L and φ_R from current geometry
   - Compute belt tensions F_L and F_R via Law of Sines
   - Find tilt angle γ (binary search ±2°) where the net torque is zero:
     `T_right − T_left + T_gravity = 0`
   - Repeat until γ converges to within 0.25°

3. **3D belt length** — the pulleys protrude 41 mm from the wall, so the actual belt path is 3-dimensional:
   `length = √(flat_distance² + 41²)`

4. **Belt dilation correction** — belts stretch under tension:
   `elongation_factor = 1 + 5×10⁻⁵ × F_belt`

5. **Convert to steps:**
   `steps = (length_mm / circumference_mm) × stepsPerRotation`

Physical constants used:
- `d_t = 76.027 mm` — distance between belt tangent points on the robot body
- `d_p = 4.487 mm` — pen offset from tangent-point line
- `d_m = 14.487 mm` — center-of-mass offset
- `mass = 0.55 kg`

---

### 5. Runner & Task System (`src/runner.cpp`, `src/tasks/`)

The Runner reads the `/commands` file from LittleFS line by line. Each line becomes a Task. Tasks are executed one at a time; the next is not started until the current reports `isDone() == true`.

**Command file format:**
```
d<total_travel_mm>          ← header: total path length
h<canvas_height_mm>         ← header: canvas height
250.3 180.7                 ← move to coordinate (x=250.3, y=180.7)
p1                          ← pen down
312.0 200.1
p0                          ← pen up
...
```

**Task types:**

| Task | Trigger | Behaviour |
|---|---|---|
| `InterpolatingMovementTask` | coordinate line | Walks to target in 1 mm increments, calling `beginLinearTravel()` each step |
| `PenTask` | `p0` / `p1` | Calls `pen->slowUp()` or `pen->slowDown()` |

The 1 mm interpolation ensures the kinematic solver is called frequently enough that the belt-length curve is followed accurately (the relationship between coordinates and belt lengths is nonlinear).

Progress is tracked as `distance_traveled / total_distance × 100 %` and shown on the OLED.

On completion the ESP32 auto-restarts.

---

### 6. Pen Control (`src/pen.cpp`)

The pen is attached to a servo. Two angles matter:
- **Up** — fixed at 90°
- **Down** — user-calibrated (e.g. 45°), stored as `penDistance`

Movement is gradual: `slowUp()` and `slowDown()` step the servo at 90°/sec in 10 ms increments to avoid mechanical shock and preserve pen contact pressure.

---

### 7. Display (`src/display.cpp`)

SSD1306 OLED over I2C. Two screens:
- **Home screen** — shows IP address and `mural.local` after boot
- **Progress screen** — shows `XX%` during drawing

---

## SVG Processing Pipeline (Browser-side TypeScript)

Located in `tsc/src/`. Built by webpack into a Web Worker (`/data/www/worker/worker.js`) that runs off the main UI thread.

The pipeline converts an uploaded image or SVG into the command file the ESP32 needs.

```
Input: SVG file  ─or─  raster image (PNG/JPEG)
         │
         ▼
[1] Vectorize (raster only)
    Potrace algorithm: bitmap → vector SVG

         ▼
[2] Parse SVG
    Extract all Path and CompoundPath objects

         ▼
[3] Scale
    Map SVG units to mm (canvas width in mm / SVG viewport width)

         ▼
[4] Infill (optional)
    Generate 45° hatch lines inside closed shapes
    Densities: 0 (none) → 4 (dense, 7 mm spacing)

         ▼
[5] Path optimisation
    Greedy nearest-neighbour sort:
    - Start from home position
    - Always pick the closest unvisited path start (or end — reverses if shorter)
    Minimises total pen-up travel

         ▼
[6] Clip to viewport
    Remove any segments outside the canvas bounds

         ▼
[7] Render to commands
    Each path segment → "x y" coordinate lines
    Pen transitions → "p0" / "p1" lines

         ▼
[8] Measure
    Walk all commands, accumulate Euclidean distances
    → total_travel_mm, canvas_height_mm

         ▼
[9] Deduplicate
    Remove consecutive identical coordinates

         ▼
Output: command file text
    d<total_travel>
    h<height>
    <x> <y>
    p1
    <x> <y>
    p0
    ...
```

The finished file is POSTed to `/uploadCommands` on the ESP32.

---

## Full End-to-End Flow: From Upload to Finished Drawing

### Phase 0 — Retract Belts
The user manually jogs the motors via the web UI (`l-ext`, `l-ret`, `r-ext`, `r-ret` commands) until the belts are correctly seated on the pulleys and the robot hangs level. Clicking "Done" advances to phase 1.

### Phase 1 — Set Top Distance
The user measures the distance between the two anchor pins on the wall and enters it in the web UI (`/setTopDistance`). The firmware stores this and computes the safe drawing rectangle (60% of pin distance wide, 80% of canvas height tall) to keep the robot away from extreme angles where kinematics break down.

Optional: `/estepsCalibration` extends a belt 1000 mm so the user can verify the steps-per-mm constant against a tape measure.

### Phase 2 — Extend to Home
`/extendToHome` is called. The firmware reels out both belts until the robot reaches the home position: horizontally centred, 350 mm below the anchors. This establishes the coordinate origin. Phase transitions automatically when motors finish.

### Phase 3 — Pen Calibration
The user tests servo angles via `/setServo` to find the angle where the pen just touches the wall surface. `/setPenDistance` locks in that angle and advances to phase 4.

### Phase 4 — SVG Select (Upload)
The user uploads an image or SVG in the browser. The Web Worker runs the full processing pipeline (vectorise → parse → scale → infill → optimise → render → measure) and POSTs the resulting command file to `/uploadCommands`. The ESP32 saves it to LittleFS at `/commands` and transitions to phase 5.

### Phase 5 — Begin Drawing
The user clicks "Run". The firmware:
1. Opens `/commands` and reads the header (`d`, `h` lines)
2. Loops:
   - Parse next line
   - If coordinate: create `InterpolatingMovementTask`
     - Walk 1 mm at a time toward target
     - Each step: run kinematic solver → set stepper targets
     - `runSteppers()` pulses motors every loop cycle until target reached
   - If `p1`/`p0`: create `PenTask` → slowly move servo to down/up angle
   - Track progress, update OLED
3. When file is exhausted, auto-restart ESP32

---

## Calibration Reference

| What | Endpoint | How |
|---|---|---|
| Belt positioning | `/command` (POST) | Jog motors manually |
| Pin separation | `/setTopDistance` (POST) | Enter measured mm |
| Steps-per-mm | `/estepsCalibration` (POST) | Extend 1000 mm, measure, adjust `diameter` constant in firmware |
| Pen-down angle | `/setServo` + `/setPenDistance` | Test angles, set final value |
| Pen-up angle | Fixed at 90° | No calibration needed |

---

## HTTP API Summary

| Endpoint | Method | Phase | Purpose |
|---|---|---|---|
| `/` | GET | all | Serve web UI |
| `/getState` | GET | all | JSON: phase, moving, x, y |
| `/command` | POST | 0 | Jog motor (`l-ext`, `r-ret`, etc.) |
| `/setTopDistance` | POST | 1 | Set anchor pin distance |
| `/estepsCalibration` | POST | 1 | Extend 1000 mm for calibration |
| `/extendToHome` | POST | 2 | Home the robot |
| `/setServo` | POST | 3 | Test servo angle |
| `/setPenDistance` | POST | 3 | Lock pen-down angle |
| `/uploadCommands` | POST | 4 | Upload command file |
| `/downloadCommands` | GET | all | Download existing command file |
| `/run` | POST | 5 | Start drawing |
| `/doneWithPhase` | POST | 5 | Reset for next job |

---

## Filesystem Layout (LittleFS)

```
/commands               ← uploaded command file, read during drawing
/www/
    index.html          ← web UI entry point
    worker/
        worker.js       ← compiled TypeScript SVG processor (Web Worker)
```

Partition table (`partitions.csv`):

| Name | Type | Size |
|---|---|---|
| nvs | data | 6 KB |
| phy_init | data | 4 KB |
| factory (firmware) | app | 1200 KB |
| spiffs (LittleFS) | data | 2800 KB |

---

## Build System

PlatformIO (`platformio.ini`) targets `esp32dev`. A pre-build Python script (`build.py`) runs before firmware compilation:

1. Clean `/data/www/worker/`
2. `npm run build` in `tsc/` — webpack bundles TypeScript → `worker.js`
3. Copy `worker.js` into `/data/www/worker/`

After building, LittleFS image is flashed separately (`pio run --target uploadfs`), then firmware is flashed normally.

**Key libraries:**

| Library | Version | Role |
|---|---|---|
| WiFiManager | 2.0.17 | WiFi provisioning |
| ESPAsyncWebServer | 3.7.4 | Non-blocking HTTP |
| AccelStepper | 1.64 | Stepper pulse generation |
| ESP32Servo | 3.0.9 | Pen servo PWM |
| Adafruit SSD1306 | 2.5.13 | OLED driver |
| ArduinoJson | 5.13.4 | JSON for `/getState` |

---

## Performance Numbers

| Metric | Value |
|---|---|
| Drawing speed | 500 steps/sec ≈ 12.5 mm/sec |
| Rapid travel speed | 1500 steps/sec ≈ 37.5 mm/sec |
| Position resolution | ~0.025 mm/step |
| Servo slew rate | 90°/sec |
| Kinematic iterations | typically 1–3 (max 20) per waypoint |
| Interpolation step | 1 mm per kinematic call |
