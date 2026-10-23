#include "no_plate_button_config.h"

namespace IndicatorNoPlate {

bool NoPlateButtonV1::begin() {
    pinMode(NO_PLATE_SIDE_BUTTON_PIN, INPUT_PULLUP);
    lastRawState_ = digitalRead(NO_PLATE_SIDE_BUTTON_PIN);
    return true;
}

SideSelection NoPlateButtonV1::selection() const {
    return digitalRead(NO_PLATE_SIDE_BUTTON_PIN) == LOW
        ? SideSelection::GRAPHIC
        : SideSelection::NORMAL;
}

bool NoPlateButtonV1::normalSide() const {
    return selection() == SideSelection::NORMAL;
}

bool NoPlateButtonV1::graphicSide() const {
    return selection() == SideSelection::GRAPHIC;
}

bool NoPlateButtonV1::changed() {
    const int current = digitalRead(NO_PLATE_SIDE_BUTTON_PIN);
    if (current == lastRawState_) return false;
    lastRawState_ = current;
    return true;
}

int NoPlateButtonV1::rawState() const {
    return digitalRead(NO_PLATE_SIDE_BUTTON_PIN);
}

} // namespace IndicatorNoPlate
