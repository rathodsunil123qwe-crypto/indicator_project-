#include "led_adafruit_transport.h"

namespace IndicatorLED {

bool AdafruitNeoPixelTransport::begin() {
    strip_.begin();
    strip_.clear();
    strip_.show();
    return true;
}

void AdafruitNeoPixelTransport::setPixel(size_t index, SceneColor color) {
    if (index >= count_) return;
    strip_.setPixelColor(index, strip_.Color(color.r, color.g, color.b));
}

void AdafruitNeoPixelTransport::clear() {
    strip_.clear();
}

bool AdafruitNeoPixelTransport::show() {
    strip_.show();
    return true;
}

} // namespace IndicatorLED
