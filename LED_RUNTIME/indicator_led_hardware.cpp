#include "indicator_led_hardware.h"

namespace IndicatorLED {

IndicatorLEDHardwareV1::IndicatorLEDHardwareV1()
    : leftFront_(LED_CFG::LEFT_FRONT_PIN, LED_CFG::FRONT_COUNT),
      leftBack_(LED_CFG::LEFT_BACK_PIN, LED_CFG::BACK_COUNT),
      rightFront_(LED_CFG::RIGHT_FRONT_PIN, LED_CFG::FRONT_COUNT),
      rightBack_(LED_CFG::RIGHT_BACK_PIN, LED_CFG::BACK_COUNT),
      noPlate_(LED_CFG::NO_PLATE_PIN, LED_CFG::NO_PLATE_COUNT),
      runtime_(leftFront_, leftBack_, rightFront_, rightBack_, noPlate_),
      scenePort_(runtime_) {}

bool IndicatorLEDHardwareV1::begin() {
    return runtime_.begin();
}

} // namespace IndicatorLED
