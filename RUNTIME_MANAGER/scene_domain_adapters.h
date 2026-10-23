#pragma once

#include "../SCENE_VM/scene_vm_v1_interface.h"
#include "../LED_RUNTIME/led_runtime_v1.h"

namespace IndicatorSceneRuntime {

// Restricts a SceneVM LEDPort to exactly one hardware LED domain while
// retaining the shared LEDRuntime framebuffer/transport underneath.
class NoPlateLEDPortAdapter final : public IndicatorSceneVM::LEDPort {
public:
    explicit NoPlateLEDPortAdapter(IndicatorLED::LEDRuntimeV1& runtime) : runtime_(runtime) {}

    bool set(IndicatorSceneVM::Side side, IndicatorSceneVM::Zone zone,
             const IndicatorSceneVM::LEDSelector& selector,
             IndicatorSceneVM::SceneColor color, bool hasBrightness,
             uint8_t brightness) override;
    bool clear(IndicatorSceneVM::Side side, IndicatorSceneVM::Zone zone,
               const IndicatorSceneVM::LEDSelector& selector) override;
    bool show() override;
    size_t zoneCount(IndicatorSceneVM::Zone zone) const override;

private:
    IndicatorLED::LEDRuntimeV1& runtime_;
};

// Indicator-domain adapter: FRONT/MAIN/LENS/BACK are visible here; the
// NO_PLATELEDS domain is deliberately inaccessible from this VM.
class IndicatorLEDPortAdapter final : public IndicatorSceneVM::LEDPort {
public:
    explicit IndicatorLEDPortAdapter(IndicatorLED::LEDRuntimeV1& runtime) : runtime_(runtime) {}

    void setSyncedOwnership(bool enabled) { syncedOwnership_ = enabled; }
    bool syncedOwnership() const { return syncedOwnership_; }

    bool set(IndicatorSceneVM::Side side, IndicatorSceneVM::Zone zone,
             const IndicatorSceneVM::LEDSelector& selector,
             IndicatorSceneVM::SceneColor color, bool hasBrightness,
             uint8_t brightness) override;
    bool clear(IndicatorSceneVM::Side side, IndicatorSceneVM::Zone zone,
               const IndicatorSceneVM::LEDSelector& selector) override;
    bool show() override;
    size_t zoneCount(IndicatorSceneVM::Zone zone) const override;

private:
    IndicatorLED::LEDRuntimeV1& runtime_;
    bool syncedOwnership_ = false;
};

} // namespace IndicatorSceneRuntime
