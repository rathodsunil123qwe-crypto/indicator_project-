#pragma once

#include "motion_types.h"

// ============================================================
// MOTION CORE CONFIGURATION V1.2
// ============================================================
// ONLY geometry and closed-loop position-control parameters live here.
// Homing, stall detection, timeouts, retry/recovery and homing LED policy are
// intentionally NOT stored in MotionAxisConfig. They are owned by the
// dedicated RECOVERY configuration so all failure/recovery tuning has one home.
extern const MotionAxisConfig kLeftMotionConfig;
extern const MotionAxisConfig kRightMotionConfig;
