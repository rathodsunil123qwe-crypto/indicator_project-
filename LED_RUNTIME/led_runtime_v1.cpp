#include "led_runtime_v1.h"
#include <cstring>


namespace IndicatorLED {

namespace {
constexpr size_t kInvalidIndex = static_cast<size_t>(-1);

uint8_t scaleChannel(uint8_t value, uint8_t brightness) {
    const uint32_t scaled = static_cast<uint32_t>(value) * brightness + 127u;
    return static_cast<uint8_t>(scaled / 255u);
}

SceneColor scaledColor(SceneColor c, uint8_t brightness) {
    return SceneColor{
        scaleChannel(c.r, brightness),
        scaleChannel(c.g, brightness),
        scaleChannel(c.b, brightness)
    };
}
} // namespace

LEDRuntimeV1::LEDRuntimeV1(LEDTransport& leftFront,
                           LEDTransport& leftBack,
                           LEDTransport& rightFront,
                           LEDTransport& rightBack,
                           LEDTransport& noPlate)
    : leftFront_(leftFront), leftBack_(leftBack),
      rightFront_(rightFront), rightBack_(rightBack), noPlate_(noPlate) {}

bool LEDRuntimeV1::begin() {
    if (leftFront_.size() != LED_CFG::FRONT_COUNT ||
        rightFront_.size() != LED_CFG::FRONT_COUNT ||
        leftBack_.size() != LED_CFG::BACK_COUNT ||
        rightBack_.size() != LED_CFG::BACK_COUNT ||
        noPlate_.size() != LED_CFG::NO_PLATE_COUNT) {
        setError("LED transport size does not match LED_CFG profile");
        return false;
    }

    if (!leftFront_.begin() || !leftBack_.begin() ||
        !rightFront_.begin() || !rightBack_.begin() || !noPlate_.begin()) {
        setError("LED transport begin failed");
        return false;
    }

    reset();
    return true;
}

void LEDRuntimeV1::reset() {
    auto clearPixels = [](Pixel* pixels, size_t count) {
        for (size_t i = 0; i < count; ++i) pixels[i] = Pixel{};
    };
    clearPixels(leftFrontPixels_, LED_CFG::FRONT_COUNT);
    clearPixels(leftBackPixels_, LED_CFG::BACK_COUNT);
    clearPixels(rightFrontPixels_, LED_CFG::FRONT_COUNT);
    clearPixels(rightBackPixels_, LED_CFG::BACK_COUNT);
    clearPixels(noPlatePixels_, LED_CFG::NO_PLATE_COUNT);
    lastError_ = "";
}

bool LEDRuntimeV1::selectorToRange(const LEDSelector& selector, size_t count,
                                   size_t& first, size_t& last) const {
    if (count == 0) {
        return false;
    }

    if (!selector.hasSelector) {
        first = 0;
        last = count - 1;
        return true;
    }

    // Scene language LED selectors are one-based physical LED numbers. The VM
    // resolves dynamic expressions to selector.index before calling LEDPort.
    // LEDRuntime converts that one-based value to the transport's zero-based
    // index exactly once. A dynamic flag changes no indexing semantics.
    if (selector.index < 1 || static_cast<uint32_t>(selector.index) > count) {
        return false;
    }

    first = static_cast<size_t>(selector.index - 1);
    last = first;
    return true;
}

bool LEDRuntimeV1::zoneToStrip(Side side, Zone zone, size_t sideOrdinal,
                               StripState& out) {
    if (side == Side::LEFT) {
        if (zone == Zone::FRONTLEDS || zone == Zone::MAINLEDS || zone == Zone::LENS) {
            out = StripState{leftFrontPixels_,
                             zone == Zone::MAINLEDS ? LED_CFG::MAIN_COUNT : LED_CFG::FRONT_COUNT,
                             &leftFront_};
            return true;
        }
        if (zone == Zone::BACKLEDS) {
            out = StripState{leftBackPixels_, LED_CFG::BACK_COUNT, &leftBack_};
            return true;
        }
        return false;
    }

    if (side == Side::RIGHT) {
        if (zone == Zone::FRONTLEDS || zone == Zone::MAINLEDS || zone == Zone::LENS) {
            out = StripState{rightFrontPixels_,
                             zone == Zone::MAINLEDS ? LED_CFG::MAIN_COUNT : LED_CFG::FRONT_COUNT,
                             &rightFront_};
            return true;
        }
        if (zone == Zone::BACKLEDS) {
            out = StripState{rightBackPixels_, LED_CFG::BACK_COUNT, &rightBack_};
            return true;
        }
        return false;
    }

    if (side == Side::BOTH && zone == Zone::NO_PLATELEDS) {
        // sideOrdinal must be zero for the single physical number-plate strip.
        if (sideOrdinal != 0) return false;
        out = StripState{noPlatePixels_, LED_CFG::NO_PLATE_COUNT, &noPlate_};
        return true;
    }

    return false;
}

bool LEDRuntimeV1::applyToStrip(StripState strip, const LEDSelector& selector,
                                SceneColor color, bool hasBrightness,
                                uint8_t brightness, bool clearOnly) {
    size_t first = kInvalidIndex;
    size_t last = kInvalidIndex;
    if (!selectorToRange(selector, strip.count, first, last)) {
        setError("LED selector is outside the configured physical range");
        return false;
    }

    // LENS is always the final FRONTLEDS pixel. The caller handles its semantic
    // zone by constructing a selector for this exact configured index.
    for (size_t i = first; i <= last; ++i) {
        if (clearOnly) {
            strip.pixels[i] = Pixel{};
        } else {
            strip.pixels[i].color = color;
            strip.pixels[i].brightness = hasBrightness ? brightness : LED_CFG::DEFAULT_BRIGHTNESS;
            strip.pixels[i].enabled = true;
        }
    }
    return true;
}

bool LEDRuntimeV1::set(Side side, Zone zone, const LEDSelector& selector,
                       SceneColor color, bool hasBrightness, uint8_t brightness) {
    if (side == Side::NONE) {
        setError("LED side NONE is invalid");
        return false;
    }

    if (zone == Zone::NO_PLATELEDS && side != Side::BOTH) {
        setError("NO_PLATELEDS requires BOTH");
        return false;
    }
    if (zone == Zone::LENS && selector.hasSelector) {
        setError("LENS does not accept a selector");
        return false;
    }

    if (zone == Zone::LENS) {
        LEDSelector lensSelector;
        lensSelector.hasSelector = true;
        lensSelector.index = static_cast<int32_t>(LED_CFG::LENS_INDEX + 1);
        if (side == Side::LEFT || side == Side::BOTH) {
            StripState s{};
            if (!zoneToStrip(Side::LEFT, zone, 0, s) ||
                !applyToStrip(s, lensSelector, color, hasBrightness, brightness, false)) return false;
        }
        if (side == Side::RIGHT || side == Side::BOTH) {
            StripState s{};
            if (!zoneToStrip(Side::RIGHT, zone, 0, s) ||
                !applyToStrip(s, lensSelector, color, hasBrightness, brightness, false)) return false;
        }
        lastError_ = "";
        return true;
    }

    if (zone == Zone::NO_PLATELEDS) {
        StripState s{};
        if (!zoneToStrip(Side::BOTH, zone, 0, s) ||
            !applyToStrip(s, selector, color, hasBrightness, brightness, false)) return false;
        lastError_ = "";
        return true;
    }

    if (side == Side::LEFT || side == Side::BOTH) {
        StripState s{};
        if (!zoneToStrip(Side::LEFT, zone, 0, s) ||
            !applyToStrip(s, selector, color, hasBrightness, brightness, false)) return false;
    }
    if (side == Side::RIGHT || side == Side::BOTH) {
        StripState s{};
        if (!zoneToStrip(Side::RIGHT, zone, 0, s) ||
            !applyToStrip(s, selector, color, hasBrightness, brightness, false)) return false;
    }

    lastError_ = "";
    return true;
}

bool LEDRuntimeV1::clear(Side side, Zone zone, const LEDSelector& selector) {
    if (side == Side::NONE) {
        setError("LED side NONE is invalid");
        return false;
    }
    if (zone == Zone::NO_PLATELEDS && side != Side::BOTH) {
        setError("NO_PLATELEDS requires BOTH");
        return false;
    }
    if (zone == Zone::LENS && selector.hasSelector) {
        setError("LENS does not accept a selector");
        return false;
    }

    if (zone == Zone::LENS) {
        LEDSelector lensSelector;
        lensSelector.hasSelector = true;
        lensSelector.index = static_cast<int32_t>(LED_CFG::LENS_INDEX + 1);
        if (side == Side::LEFT || side == Side::BOTH) {
            StripState s{};
            if (!zoneToStrip(Side::LEFT, zone, 0, s) ||
                !applyToStrip(s, lensSelector, SceneColor{0,0,0}, false, 0, true)) return false;
        }
        if (side == Side::RIGHT || side == Side::BOTH) {
            StripState s{};
            if (!zoneToStrip(Side::RIGHT, zone, 0, s) ||
                !applyToStrip(s, lensSelector, SceneColor{0,0,0}, false, 0, true)) return false;
        }
        lastError_ = "";
        return true;
    }

    if (zone == Zone::NO_PLATELEDS) {
        StripState s{};
        if (!zoneToStrip(Side::BOTH, zone, 0, s) ||
            !applyToStrip(s, selector, SceneColor{0,0,0}, false, 0, true)) return false;
        lastError_ = "";
        return true;
    }

    if (side == Side::LEFT || side == Side::BOTH) {
        StripState s{};
        if (!zoneToStrip(Side::LEFT, zone, 0, s) ||
            !applyToStrip(s, selector, SceneColor{0,0,0}, false, 0, true)) return false;
    }
    if (side == Side::RIGHT || side == Side::BOTH) {
        StripState s{};
        if (!zoneToStrip(Side::RIGHT, zone, 0, s) ||
            !applyToStrip(s, selector, SceneColor{0,0,0}, false, 0, true)) return false;
    }

    lastError_ = "";
    return true;
}

void LEDRuntimeV1::writeStrip(StripState strip) {
    for (size_t i = 0; i < strip.count; ++i) {
        const Pixel& p = strip.pixels[i];
        strip.transport->setPixel(i, p.enabled ? scaledColor(p.color, p.brightness)
                                                : SceneColor{0,0,0});
    }
}

bool LEDRuntimeV1::captureIndicatorOutputs(IndicatorOutputSnapshot& out) const {
    std::memcpy(out.leftFront, leftFrontPixels_, sizeof(leftFrontPixels_));
    std::memcpy(out.leftBack, leftBackPixels_, sizeof(leftBackPixels_));
    std::memcpy(out.rightFront, rightFrontPixels_, sizeof(rightFrontPixels_));
    std::memcpy(out.rightBack, rightBackPixels_, sizeof(rightBackPixels_));
    out.valid = true;
    return true;
}

bool LEDRuntimeV1::captureNoPlateOutputs(NoPlateOutputSnapshot& out) const {
    std::memcpy(out.noPlate, noPlatePixels_, sizeof(noPlatePixels_));
    out.valid = true;
    return true;
}

bool LEDRuntimeV1::restoreNoPlateOutputs(const NoPlateOutputSnapshot& in) {
    if (!in.valid) {
        setError("invalid NO_PLATE LED snapshot");
        return false;
    }
    std::memcpy(noPlatePixels_, in.noPlate, sizeof(noPlatePixels_));
    return showNoPlate();
}

bool LEDRuntimeV1::restoreIndicatorOutputs(const IndicatorOutputSnapshot& in) {
    if (!in.valid) {
        setError("invalid INDICATOR LED snapshot");
        return false;
    }
    std::memcpy(leftFrontPixels_, in.leftFront, sizeof(leftFrontPixels_));
    std::memcpy(leftBackPixels_, in.leftBack, sizeof(leftBackPixels_));
    std::memcpy(rightFrontPixels_, in.rightFront, sizeof(rightFrontPixels_));
    std::memcpy(rightBackPixels_, in.rightBack, sizeof(rightBackPixels_));
    return showIndicator();
}

bool LEDRuntimeV1::clearIndicatorOutputs() {
    auto clearPixels = [](Pixel* pixels, size_t count) {
        for (size_t i = 0; i < count; ++i) pixels[i] = Pixel{};
    };

    // STOP owns only the INDICATOR domain. Never clear NO_PLATE here.
    clearPixels(leftFrontPixels_, LED_CFG::FRONT_COUNT);
    clearPixels(leftBackPixels_, LED_CFG::BACK_COUNT);
    clearPixels(rightFrontPixels_, LED_CFG::FRONT_COUNT);
    clearPixels(rightBackPixels_, LED_CFG::BACK_COUNT);

    // Commit only the INDICATOR domain. NO_PLATE is not retransmitted.
    return showIndicator();
}

bool LEDRuntimeV1::showIndicator() {
    writeStrip(StripState{leftFrontPixels_, LED_CFG::FRONT_COUNT, &leftFront_});
    writeStrip(StripState{leftBackPixels_, LED_CFG::BACK_COUNT, &leftBack_});
    writeStrip(StripState{rightFrontPixels_, LED_CFG::FRONT_COUNT, &rightFront_});
    writeStrip(StripState{rightBackPixels_, LED_CFG::BACK_COUNT, &rightBack_});

    if (!leftFront_.show() || !leftBack_.show() ||
        !rightFront_.show() || !rightBack_.show()) {
        setError("INDICATOR LED transport show failed");
        return false;
    }
    lastError_ = "";
    return true;
}

bool LEDRuntimeV1::showNoPlate() {
    writeStrip(StripState{noPlatePixels_, LED_CFG::NO_PLATE_COUNT, &noPlate_});
    if (!noPlate_.show()) {
        setError("NO_PLATE LED transport show failed");
        return false;
    }
    lastError_ = "";
    return true;
}

bool LEDRuntimeV1::show() {
    // Full-frame compatibility path. Scene-domain adapters use the domain
    // specific methods above so ordinary dual-VM SHOW operations do not
    // retransmit unrelated LED strips.
    return showIndicator() && showNoPlate();
}

} // namespace IndicatorLED
