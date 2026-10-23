#include "event_manager_v1.h"

namespace IndicatorEventManager {

bool EventManagerV1::translateSerial(const String& input, Event& out) const {
    out = Event{};

    String command = input;
    command.trim();
    command.toLowerCase();

    if (command == "l") {
        out.type = EventType::LEFT;
        return true;
    }

    if (command == "r") {
        out.type = EventType::RIGHT;
        return true;
    }

    if (command.length() == 0) return false;

    for (size_t i = 0; i < command.length(); ++i) {
        if (command[i] < '0' || command[i] > '9') return false;
    }

    const int clicks = command.toInt();
    if (clicks < 1 || clicks > 32) return false;

    out.type = static_cast<EventType>(
        static_cast<uint8_t>(EventType::MIDDLE_1_CLICK) +
        static_cast<uint8_t>(clicks - 1));
    out.middleClickCount = static_cast<uint8_t>(clicks);
    return true;
}

const char* EventManagerV1::name(EventType type) {
    switch (type) {
        case EventType::LEFT: return "EVENT_LEFT";
        case EventType::RIGHT: return "EVENT_RIGHT";
        case EventType::MIDDLE_1_CLICK: return "EVENT_MIDDLE_1_CLICK";
        case EventType::MIDDLE_2_CLICKS: return "EVENT_MIDDLE_2_CLICKS";
        case EventType::MIDDLE_3_CLICKS: return "EVENT_MIDDLE_3_CLICKS";
        case EventType::MIDDLE_4_CLICKS: return "EVENT_MIDDLE_4_CLICKS";
        case EventType::MIDDLE_5_CLICKS: return "EVENT_MIDDLE_5_CLICKS";
        case EventType::MIDDLE_6_CLICKS: return "EVENT_MIDDLE_6_CLICKS";
        case EventType::MIDDLE_7_CLICKS: return "EVENT_MIDDLE_7_CLICKS";
        case EventType::MIDDLE_8_CLICKS: return "EVENT_MIDDLE_8_CLICKS";
        case EventType::MIDDLE_9_CLICKS: return "EVENT_MIDDLE_9_CLICKS";
        case EventType::MIDDLE_10_CLICKS: return "EVENT_MIDDLE_10_CLICKS";
        case EventType::MIDDLE_11_CLICKS: return "EVENT_MIDDLE_11_CLICKS";
        case EventType::MIDDLE_12_CLICKS: return "EVENT_MIDDLE_12_CLICKS";
        case EventType::MIDDLE_13_CLICKS: return "EVENT_MIDDLE_13_CLICKS";
        case EventType::MIDDLE_14_CLICKS: return "EVENT_MIDDLE_14_CLICKS";
        case EventType::MIDDLE_15_CLICKS: return "EVENT_MIDDLE_15_CLICKS";
        case EventType::MIDDLE_16_CLICKS: return "EVENT_MIDDLE_16_CLICKS";
        case EventType::MIDDLE_17_CLICKS: return "EVENT_MIDDLE_17_CLICKS";
        case EventType::MIDDLE_18_CLICKS: return "EVENT_MIDDLE_18_CLICKS";
        case EventType::MIDDLE_19_CLICKS: return "EVENT_MIDDLE_19_CLICKS";
        case EventType::MIDDLE_20_CLICKS: return "EVENT_MIDDLE_20_CLICKS";
        case EventType::MIDDLE_21_CLICKS: return "EVENT_MIDDLE_21_CLICKS";
        case EventType::MIDDLE_22_CLICKS: return "EVENT_MIDDLE_22_CLICKS";
        case EventType::MIDDLE_23_CLICKS: return "EVENT_MIDDLE_23_CLICKS";
        case EventType::MIDDLE_24_CLICKS: return "EVENT_MIDDLE_24_CLICKS";
        case EventType::MIDDLE_25_CLICKS: return "EVENT_MIDDLE_25_CLICKS";
        case EventType::MIDDLE_26_CLICKS: return "EVENT_MIDDLE_26_CLICKS";
        case EventType::MIDDLE_27_CLICKS: return "EVENT_MIDDLE_27_CLICKS";
        case EventType::MIDDLE_28_CLICKS: return "EVENT_MIDDLE_28_CLICKS";
        case EventType::MIDDLE_29_CLICKS: return "EVENT_MIDDLE_29_CLICKS";
        case EventType::MIDDLE_30_CLICKS: return "EVENT_MIDDLE_30_CLICKS";
        case EventType::MIDDLE_31_CLICKS: return "EVENT_MIDDLE_31_CLICKS";
        case EventType::MIDDLE_32_CLICKS: return "EVENT_MIDDLE_32_CLICKS";
        default: return "EVENT_NONE";
    }
}

} // namespace IndicatorEventManager
