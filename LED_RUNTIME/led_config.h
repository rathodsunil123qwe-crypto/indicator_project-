#pragma once

#include <stdint.h>
#include <stddef.h>

// ============================================================
// INDICATOR LED RUNTIME V1 — SINGLE SOURCE OF TRUTH
// ============================================================
// This file owns the physical LED profile. All runtime geometry is derived
// from these values; no other source file may hard-code LED counts or the
// FRONTLEDS/MAINLEDS/LENS relationship.
//
// Physical rule locked for V1:
//   FRONTLEDS = complete front strip.
//   MAINLEDS  = every FRONTLEDS pixel except the final pixel.
//   LENS      = final FRONTLEDS pixel.
//
// Therefore changing FRONT_COUNT automatically changes MAIN_COUNT and the
// LENS physical index. Example: 10 -> MAIN 9 + LENS 10; 12 -> MAIN 11 +
// LENS 12. LENS_INDEX is zero-based because the transport uses zero-based
// pixel indices; scene selectors remain one-based at the scene-language
// boundary and are converted exactly once by the runtime.
namespace LED_CFG {

constexpr uint8_t LEFT_FRONT_PIN   = 22;
constexpr uint8_t LEFT_BACK_PIN    = 23;
constexpr uint8_t RIGHT_FRONT_PIN  = 18;
constexpr uint8_t RIGHT_BACK_PIN   = 19;
constexpr uint8_t NO_PLATE_PIN     = 21;

constexpr uint16_t FRONT_COUNT     = 10;
constexpr uint16_t MAIN_COUNT      = FRONT_COUNT - 1;
constexpr uint16_t LENS_INDEX      = FRONT_COUNT - 1; // zero-based
constexpr uint16_t BACK_COUNT      = 8;
constexpr uint16_t NO_PLATE_COUNT  = 7;

static_assert(FRONT_COUNT >= 2, "FRONT_COUNT must include MAINLEDS and LENS");
static_assert(MAIN_COUNT == FRONT_COUNT - 1, "MAINLEDS must exclude final LENS pixel");
static_assert(LENS_INDEX == FRONT_COUNT - 1, "LENS must be final FRONTLEDS pixel");

constexpr uint8_t DEFAULT_BRIGHTNESS = 255;
constexpr uint8_t PROFILE_VERSION = 1;

} // namespace LED_CFG
