#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace IndicatorEventManager {

enum class EventType : uint8_t {
    NONE = 0,
    LEFT,
    RIGHT,
    MIDDLE_1_CLICK,
    MIDDLE_2_CLICKS,
    MIDDLE_3_CLICKS,
    MIDDLE_4_CLICKS,
    MIDDLE_5_CLICKS,
    MIDDLE_6_CLICKS,
    MIDDLE_7_CLICKS,
    MIDDLE_8_CLICKS,
    MIDDLE_9_CLICKS,
    MIDDLE_10_CLICKS,
    MIDDLE_11_CLICKS,
    MIDDLE_12_CLICKS,
    MIDDLE_13_CLICKS,
    MIDDLE_14_CLICKS,
    MIDDLE_15_CLICKS,
    MIDDLE_16_CLICKS,
    MIDDLE_17_CLICKS,
    MIDDLE_18_CLICKS,
    MIDDLE_19_CLICKS,
    MIDDLE_20_CLICKS,
    MIDDLE_21_CLICKS,
    MIDDLE_22_CLICKS,
    MIDDLE_23_CLICKS,
    MIDDLE_24_CLICKS,
    MIDDLE_25_CLICKS,
    MIDDLE_26_CLICKS,
    MIDDLE_27_CLICKS,
    MIDDLE_28_CLICKS,
    MIDDLE_29_CLICKS,
    MIDDLE_30_CLICKS,
    MIDDLE_31_CLICKS,
    MIDDLE_32_CLICKS
};

struct Event {
    EventType type = EventType::NONE;
    uint8_t middleClickCount = 0;

    bool valid() const { return type != EventType::NONE; }
};

class EventManagerV1 {
public:
    // Translate temporary serial test vocabulary into normalized events.
    // This class does not execute runtime behavior.
    bool translateSerial(const String& input, Event& out) const;

    static const char* name(EventType type);
};

} // namespace IndicatorEventManager
