#include "scene_domain_adapters.h"

namespace IndicatorSceneRuntime {

using namespace IndicatorSceneVM;

bool NoPlateLEDPortAdapter::set(Side side, Zone zone, const LEDSelector& selector,
                                SceneColor color, bool hasBrightness, uint8_t brightness) {
    return zone == Zone::NO_PLATELEDS && side == Side::BOTH &&
           runtime_.set(side, zone, selector, color, hasBrightness, brightness);
}

bool NoPlateLEDPortAdapter::clear(Side side, Zone zone, const LEDSelector& selector) {
    return zone == Zone::NO_PLATELEDS && side == Side::BOTH &&
           runtime_.clear(side, zone, selector);
}

bool NoPlateLEDPortAdapter::show() { return runtime_.showNoPlate(); }

size_t NoPlateLEDPortAdapter::zoneCount(Zone zone) const {
    return zone == Zone::NO_PLATELEDS ? LED_CFG::NO_PLATE_COUNT : 0;
}

bool IndicatorLEDPortAdapter::set(Side side, Zone zone, const LEDSelector& selector,
                                  SceneColor color, bool hasBrightness, uint8_t brightness) {
    if (zone == Zone::NO_PLATELEDS && !syncedOwnership_) return false;
    return runtime_.set(side, zone, selector, color, hasBrightness, brightness);
}

bool IndicatorLEDPortAdapter::clear(Side side, Zone zone, const LEDSelector& selector) {
    if (zone == Zone::NO_PLATELEDS && !syncedOwnership_) return false;
    return runtime_.clear(side, zone, selector);
}

bool IndicatorLEDPortAdapter::show() {
    return syncedOwnership_ ? runtime_.show() : runtime_.showIndicator();
}

size_t IndicatorLEDPortAdapter::zoneCount(Zone zone) const {
    switch (zone) {
        case Zone::FRONTLEDS: return LED_CFG::FRONT_COUNT;
        case Zone::MAINLEDS: return LED_CFG::MAIN_COUNT;
        case Zone::LENS: return 1;
        case Zone::BACKLEDS: return LED_CFG::BACK_COUNT;
        case Zone::NO_PLATELEDS: return syncedOwnership_ ? LED_CFG::NO_PLATE_COUNT : 0;
    }
    return 0;
}

} // namespace IndicatorSceneRuntime
