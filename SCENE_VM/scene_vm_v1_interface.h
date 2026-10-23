#pragma once

// INDICATOR SCENE VM V1 — FROZEN INTERFACE CONTRACT
//
// IMPORTANT PROJECT RULE:
// This header defines the boundary between SceneVM and the proven hardware
// domains. It is an interface contract, not a motor/LED implementation.
// Do not place PID, encoder, homing, stall recovery, WS2812 transport, or
// physical pin logic in this layer.

#include <stdint.h>
#include <stddef.h>

namespace IndicatorSceneVM {

enum class Side : uint8_t { NONE = 0, LEFT = 1, RIGHT = 2, BOTH = 3 };
enum class Zone : uint8_t { FRONTLEDS, MAINLEDS, LENS, BACKLEDS, NO_PLATELEDS };

enum class VMState : uint8_t {
    IDLE,
    RUNNING,
    PAUSED,
    WAITING_TIME,
    WAITING_TARGET,
    COMPLETE,
    ERROR
};

enum class VMResult : uint8_t {
    OK,
    BUSY,
    INVALID,
    FAILED
};

struct SceneColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct SceneContext {
    // External controller metadata only. V9.1 has no SIGNAL language target.
    Side triggeredSide = Side::NONE;
};

struct LEDSelector {
    // hasSelector=false means the semantic zone as a whole.
    bool hasSelector = false;
    bool dynamic = false;
    int32_t index = 0;
};

class MotionPort {
public:
    virtual ~MotionPort() = default;

    // POS is 0..100 percent. SPEED is 1..100.
    virtual bool move(Side side, uint8_t posPercent, uint8_t speedPercent) = 0;
    virtual bool stop(Side side) = 0;

    // WAIT TARGET must be cooperative: return true only when the requested
    // motion target has been reached. A separate fault query reports failure.
    virtual bool targetReached(Side side) const = 0;
    virtual bool faulted() const = 0;
    virtual const char* faultText() const = 0;
};

class LEDPort {
public:
    virtual ~LEDPort() = default;

    // If hasBrightness=false, the VM deliberately supplies no hidden value.
    // The LED domain owns the defined default-brightness behavior.
    virtual bool set(Side side, Zone zone, const LEDSelector& selector,
                     SceneColor color, bool hasBrightness, uint8_t brightness) = 0;
    virtual bool clear(Side side, Zone zone, const LEDSelector& selector) = 0;
    virtual bool show() = 0;

    // Physical LED counts are owned by the hardware LED_CONFIG domain.
    // SceneVM asks the LED adapter instead of carrying a second hard-coded
    // copy of the physical LED counts. The adapter must return the number of
    // addressable LEDs for the requested semantic zone. LENS returns 1.
    virtual size_t zoneCount(Zone zone) const = 0;
};

class SceneVM {
public:
    virtual ~SceneVM() = default;

    virtual void begin(MotionPort* motion, LEDPort* leds) = 0;
    virtual VMResult load(const char* sceneText, size_t length) = 0;
    virtual VMResult start(const SceneContext& context = SceneContext{}) = 0;
    virtual void update(uint32_t nowMs) = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual VMResult resume() = 0;

    virtual VMState state() const = 0;
    virtual bool active() const = 0;
    virtual bool paused() const = 0;
    virtual const char* lastError() const = 0;
};

// TIME and DELTA are VM-owned read-only NUMBER values:
// TIME  = scene-relative elapsed milliseconds.
// DELTA = milliseconds since previous VM update; first tick is 0.
// The VM implementation must reset both timing values on start().

} // namespace IndicatorSceneVM
