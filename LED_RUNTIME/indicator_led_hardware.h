#pragma once

#include "led_config.h"
#include "led_adafruit_transport.h"

namespace IndicatorLED {

// Complete physical LED block for the current ESP32 hardware profile.
// Change physical counts/pins only in led_config.h; this object derives all
// strip sizes from that profile and exposes the runtime through SceneLEDPortAdapter.
class IndicatorLEDHardwareV1 {
public:
    IndicatorLEDHardwareV1();

    bool begin();
    LEDRuntimeV1& runtime() { return runtime_; }
    SceneLEDPortAdapter& scenePort() { return scenePort_; }

private:
    AdafruitNeoPixelTransport leftFront_;
    AdafruitNeoPixelTransport leftBack_;
    AdafruitNeoPixelTransport rightFront_;
    AdafruitNeoPixelTransport rightBack_;
    AdafruitNeoPixelTransport noPlate_;
    LEDRuntimeV1 runtime_;
    SceneLEDPortAdapter scenePort_;
};

} // namespace IndicatorLED
