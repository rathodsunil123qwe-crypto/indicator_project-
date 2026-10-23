#pragma once

#include <stdint.h>
#include <stddef.h>
#include "led_config.h"
#include "../SCENE_VM/scene_vm_v1_interface.h"

namespace IndicatorLED {

using IndicatorSceneVM::LEDSelector;
using IndicatorSceneVM::SceneColor;
using IndicatorSceneVM::Side;
using IndicatorSceneVM::Zone;

struct Pixel {
    SceneColor color{0, 0, 0};
    uint8_t brightness = LED_CFG::DEFAULT_BRIGHTNESS;
    bool enabled = false;
};

struct IndicatorOutputSnapshot {
    Pixel leftFront[LED_CFG::FRONT_COUNT]{};
    Pixel leftBack[LED_CFG::BACK_COUNT]{};
    Pixel rightFront[LED_CFG::FRONT_COUNT]{};
    Pixel rightBack[LED_CFG::BACK_COUNT]{};
    bool valid = false;
};

struct NoPlateOutputSnapshot {
    Pixel noPlate[LED_CFG::NO_PLATE_COUNT]{};
    bool valid = false;
};

class LEDTransport {
public:
    virtual ~LEDTransport() = default;
    virtual bool begin() = 0;
    virtual void setPixel(size_t index, SceneColor color) = 0;
    virtual void clear() = 0;
    virtual bool show() = 0;
    virtual size_t size() const = 0;
};

// Pure scene-facing LED runtime. It owns logical LED state and maps the
// SceneVM zones onto the physical strips. It deliberately knows nothing about
// WS2812 library APIs or ESP32 GPIO operations.
class LEDRuntimeV1 {
public:
    LEDRuntimeV1(LEDTransport& leftFront,
                 LEDTransport& leftBack,
                 LEDTransport& rightFront,
                 LEDTransport& rightBack,
                 LEDTransport& noPlate);

    bool begin();

    bool set(Side side, Zone zone, const LEDSelector& selector,
             SceneColor color, bool hasBrightness, uint8_t brightness);
    bool clear(Side side, Zone zone, const LEDSelector& selector);
    bool show();
    // Domain-specific commits prevent one SceneVM from transmitting the other
    // domain's LED strips and therefore keep VM LED timing independent.
    bool showIndicator();
    bool showNoPlate();
    // Clear only the INDICATOR LED domain; NO_PLATE logical state is preserved.
    bool clearIndicatorOutputs();
    bool captureIndicatorOutputs(IndicatorOutputSnapshot& out) const;
    bool restoreIndicatorOutputs(const IndicatorOutputSnapshot& in);
    bool captureNoPlateOutputs(NoPlateOutputSnapshot& out) const;
    bool restoreNoPlateOutputs(const NoPlateOutputSnapshot& in);

    const char* lastError() const { return lastError_; }
    void reset();

private:
    struct StripState {
        Pixel* pixels = nullptr;
        size_t count = 0;
        LEDTransport* transport = nullptr;
    };

    LEDTransport& leftFront_;
    LEDTransport& leftBack_;
    LEDTransport& rightFront_;
    LEDTransport& rightBack_;
    LEDTransport& noPlate_;

    Pixel leftFrontPixels_[LED_CFG::FRONT_COUNT]{};
    Pixel leftBackPixels_[LED_CFG::BACK_COUNT]{};
    Pixel rightFrontPixels_[LED_CFG::FRONT_COUNT]{};
    Pixel rightBackPixels_[LED_CFG::BACK_COUNT]{};
    Pixel noPlatePixels_[LED_CFG::NO_PLATE_COUNT]{};

    const char* lastError_ = "";

    bool applyToStrip(StripState strip, const LEDSelector& selector,
                      SceneColor color, bool hasBrightness, uint8_t brightness,
                      bool clearOnly);
    bool zoneToStrip(Side side, Zone zone, size_t sideOrdinal,
                     StripState& out);
    bool selectorToRange(const LEDSelector& selector, size_t count,
                         size_t& first, size_t& last) const;
    void setError(const char* message) { lastError_ = message; }
    void writeStrip(StripState strip);
};

// Adapter implementing the frozen SceneVM LEDPort contract.
// It is intentionally thin: all physical geometry and buffering remain in
// LEDRuntimeV1.
class SceneLEDPortAdapter final : public IndicatorSceneVM::LEDPort {
public:
    explicit SceneLEDPortAdapter(LEDRuntimeV1& runtime) : runtime_(runtime) {}

    bool set(Side side, Zone zone, const LEDSelector& selector,
             SceneColor color, bool hasBrightness, uint8_t brightness) override {
        return runtime_.set(side, zone, selector, color, hasBrightness, brightness);
    }
    bool clear(Side side, Zone zone, const LEDSelector& selector) override {
        return runtime_.clear(side, zone, selector);
    }
    bool show() override { return runtime_.show(); }
    size_t zoneCount(Zone zone) const override {
        switch (zone) {
            case Zone::FRONTLEDS: return LED_CFG::FRONT_COUNT;
            case Zone::MAINLEDS: return LED_CFG::MAIN_COUNT;
            case Zone::LENS: return 1;
            case Zone::BACKLEDS: return LED_CFG::BACK_COUNT;
            case Zone::NO_PLATELEDS: return LED_CFG::NO_PLATE_COUNT;
        }
        return 0;
    }

private:
    LEDRuntimeV1& runtime_;
};

} // namespace IndicatorLED
