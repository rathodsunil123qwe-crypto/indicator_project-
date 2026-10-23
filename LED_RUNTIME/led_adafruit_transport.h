#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include "led_runtime_v1.h"

namespace IndicatorLED {

// ESP32/WS2812 transport. This is the only source file that knows the
// NeoPixel library. LEDRuntimeV1 remains portable and testable on a host.
class AdafruitNeoPixelTransport final : public LEDTransport {
public:
    AdafruitNeoPixelTransport(uint8_t pin, uint16_t count)
        : strip_(count, pin, NEO_GRB + NEO_KHZ800), count_(count) {}

    bool begin() override;
    void setPixel(size_t index, SceneColor color) override;
    void clear() override;
    bool show() override;
    size_t size() const override { return count_; }

private:
    Adafruit_NeoPixel strip_;
    uint16_t count_;
};

} // namespace IndicatorLED
