# Plan for Speed Enhancement

## Goal

Reduce drawing time by 50–60% without altering the polargraph physics or risking mechanical damage. All changes are conservative, reversible, and independently testable.

---

## Change 1 — Remove verbose logging from `beginLinearTravel()`

**File:** `src/movement.cpp`

**Problem:** `addLog()` (→ `Serial.println()`) is called on every single 1 mm waypoint. At 9600 baud, a ~100-char serial write takes several milliseconds of blocking time. This accumulates to hundreds of milliseconds of dead time per 100 mm of travel.

**Fix:** Remove the `addLog(...)` call inside `beginLinearTravel()`. Phase-transition logs and progress logs elsewhere are infrequent and can stay.

**Risk:** None. This is a pure debug artifact with no functional effect.

**Expected gain:** 10–30% on dense drawings.

---

## Change 2 — Increase drawing speed constant

**File:** `src/movement.h`

**Problem:** `printSpeedSteps = 500` steps/sec gives ~6.2 mm/sec at 1/16 microstepping — very conservative for a TMC2209 + NEMA17 combination.

**Fix:** Raise to `800` steps/sec (~10 mm/sec). This is within the normal operating range of most polargraph builds and well below the torque drop-off region of a NEMA17 at this driver configuration.

**Risk:** Low. If pendulum oscillation becomes visible at 800, reduce to 650. `moveSpeedSteps` (rapid travel, pen-up moves) is left unchanged at 1500.

**Expected gain:** ~60% faster on all drawing moves.

---

## Change 3 — Increase interpolation step size

**File:** `src/tasks/interpolatingmovementtask.h`

**Problem:** `INCREMENT = 1` mm means the motors come to a full stop every 1 mm to recompute kinematics. Each stop-start cycle wastes time equal to roughly one loop iteration of dead time.

**Fix:** Raise `INCREMENT` to `3` mm. The belt-length nonlinearity over 3 mm on a typical 500–800 mm wide canvas produces a position error well under 0.1 mm — imperceptible in practice.

**Risk:** Negligible accuracy trade-off. If drawing quality is unacceptable for a particular image, reducing back to 2 or 1 is a one-line change.

**Expected gain:** ~33% reduction in stop-start overhead, especially on long straight strokes.

---

## Change 4 — Faster pen-up and shorter settle delay

**Files:** `src/pen.h`, `src/pen.cpp`

**Problem A:** `slowSpeedDegPerSec = 90` is applied equally to pen-up and pen-down. Pen-down needs to be gentle (approaching the wall surface). Pen-up is lifting away — there is no contact risk and it can be much faster.

**Fix A:** Introduce separate constants: `slowDownSpeedDegPerSec = 90` (unchanged), `slowUpSpeedDegPerSec = 270` (3× faster lift).

**Problem B:** `doSlowMove()` ends with an unconditional `delay(200)` settle pause after every pen transition. 200 ms is overly conservative — the servo physically completes its move well before this.

**Fix B:** Reduce settle delay to `50` ms.

**Risk:** Low. The pen-down speed is unchanged, so wall contact pressure is unaffected. The shorter settle time may need to be increased back to 100 ms if the servo is observed to still be moving at the start of the next motor command (servo-specific behaviour).

**Expected gain:** ~150 ms saved per pen transition. On a drawing with 200 strokes, that is ~30 seconds.

---

## Combined Expected Outcome

| Change | Gain |
|---|---|
| Remove logging in beginLinearTravel | 10–30% on dense drawings |
| printSpeedSteps 500 → 800 | ~60% on all moves |
| INCREMENT 1 mm → 3 mm | ~33% reduction in stop-start idle |
| Faster pen-up + shorter settle | ~30 s on stroke-heavy drawings |

Total expected reduction in drawing time: **50–60%** for a typical complex drawing.

---

## Rollback

Every change is a single constant. To revert individually:

| Change | Revert |
|---|---|
| Speed | `printSpeedSteps = 500` |
| Increment | `INCREMENT = 1` |
| Pen-up speed | `slowUpSpeedDegPerSec = 90` |
| Settle delay | `delay(200)` |
