#include <Arduino.h>
#include <string.h>

// ============================================================
// INDICATOR V3 - CURRENT VALIDATED FIRMWARE BASELINE
//
// Includes the proven baseline foundation plus the currently
// validated runtime blocks:
//   - Scene Storage V1
//   - Assignment Config
//   - WELCOME Runtime
//   - Universal TURN SIGNAL LEFT/RIGHT context
//   - Event Manager V1 -> Runtime event behaviours
//
// Current Runtime behaviours:
//   EVENT_LEFT -> TURN SIGNAL LEFT
//   EVENT_RIGHT -> TURN SIGNAL RIGHT
//   EVENT_MIDDLE_1_CLICK -> STOP / RESUME INDICATOR
//   EVENT_MIDDLE_2_CLICKS -> STOP ALL INDICATOR + discard paused scene + return both motors to 0
//
// 2-click, 3..32-click assignment behaviours and timer/pause features remain future blocks.
// ============================================================

#include "SCENE_VM/scene_vm_v1.cpp"
#include "SCENE_VM/scene_vm_v1_factory.h"
#include "MOTOR_MOTION/motor_motion_bundle.cpp"
#include "LED_RUNTIME/led_runtime_v1.cpp"
#include "LED_RUNTIME/led_adafruit_transport.cpp"
#include "LED_RUNTIME/indicator_led_hardware.cpp"
#include "INTEGRATION/motion_status_lighting.cpp"
#include "INTEGRATION/scene_motion_adapter.cpp"
#include "INTEGRATION/scene_motion_port_adapter.cpp"
#include "SCENE_STORAGE/scene_storage_v1.cpp"
#include "ASSIGNMENT_CONFIG/assignment_config_v1.cpp"
#include "RUNTIME_MANAGER/scene_domain_adapters.cpp"
#include "RUNTIME_MANAGER/scene_runtime_manager_v1.cpp"
#include "NO_PLATE/no_plate_button_config.cpp"
#include "NO_PLATE/no_plate_lock_servo_v1.cpp"
#include "EVENT_MANAGER/event_manager_v1.cpp"
#include "PHYSICAL_CONTROLLER/physical_controller_v1.cpp"
#include "UPLOAD_MANAGER/scene_assignment_upload_v1.cpp"
#include "APP_COMMANDS/app_commands_v1.cpp"
#include "APP_PROTOCOL/app_protocol_v1.cpp"
#include "APP_BLE_TRANSPORT/app_ble_transport_v1.cpp"

#include "LED_RUNTIME/led_runtime_v1.h"

MotorMotion g_motorMotion;
IndicatorLED::IndicatorLEDHardwareV1 g_ledHardware;
MotionSafetyLighting g_motionLights(
    g_ledHardware.runtime(), kLeftRecoveryConfig, kRightRecoveryConfig);
SceneMotionAdapter g_sceneMotion(g_motorMotion);
SceneMotionPortAdapter g_sceneMotionPort(g_sceneMotion);
IndicatorSceneStorage::SceneStorageV1 g_sceneStorage;
IndicatorSceneVM::SceneVM* g_noPlateVm = nullptr;
IndicatorSceneVM::SceneVM* g_indicatorVm = nullptr;
IndicatorSceneRuntime::SceneRuntimeManagerV1 g_runtime;
IndicatorNoPlate::NoPlateButtonV1 g_noPlateButton;
IndicatorEventManager::EventManagerV1 g_eventManager;
IndicatorPhysicalController::PhysicalControllerV1 g_physicalController;
IndicatorSceneUpload::SceneAssignmentUploadV1 g_uploadManager;
// App-facing command gateway. Physical controller and future Android transport
// both ultimately feed the same Runtime/Event path through this object.
IndicatorAppCommands::AppCommandInterfaceV1 g_appCommands;
// JSON protocol layer above APP_COMMANDS. Future Wi-Fi/BLE transports will feed
// complete frames into this object instead of talking to Runtime/Motion directly.
IndicatorAppProtocol::AppProtocolV1 g_appProtocol;
// BLE transport sits above APP_PROTOCOL and never talks to runtime code directly.
IndicatorAppBleTransport::AppBleTransportV1 g_appBleTransport;

static bool g_ready = false;
static bool g_homed = false;
static bool g_welcomePlayed = false;
static uint32_t g_lastStatusMs = 0;

static bool bothHomed() {
    const MotionStatus left = g_motorMotion.status(MotionAxis::LEFT);
    const MotionStatus right = g_motorMotion.status(MotionAxis::RIGHT);
    return left.homed && right.homed &&
           left.state == MotionState::AT_TARGET &&
           right.state == MotionState::AT_TARGET;
}

static const char* sideName(IndicatorSceneVM::Side side) {
    switch (side) {
        case IndicatorSceneVM::Side::LEFT:  return "LEFT";
        case IndicatorSceneVM::Side::RIGHT: return "RIGHT";
        default:                            return "NONE";
    }
}

static void printStatus() {
    const MotionStatus left = g_motorMotion.status(MotionAxis::LEFT);
    const MotionStatus right = g_motorMotion.status(MotionAxis::RIGHT);

    Serial.printf(
        "[STATUS] L=%s %ld | R=%s %ld | NO_PLATE_VM=%d | INDICATOR_VM=%d | INDICATOR_PAUSED=%d | TURN_CONTEXT=%s | NO_PLATE_GPIO17=%s\n",
        motionStateName(left.state), (long)left.encoderCounts,
        motionStateName(right.state), (long)right.encoderCounts,
        g_runtime.noPlateActive() ? 1 : 0,
        g_runtime.indicatorActive() ? 1 : 0,
        g_runtime.indicatorPaused() ? 1 : 0,
        sideName(g_runtime.activeTurnSignalSide()),
        g_noPlateButton.normalSide() ? "HIGH/NORMAL" : "LOW/GRAPHIC");
}

static void playWelcome() {
    if (!g_ready || g_runtime.indicatorActive()) return;

    Serial.println();
    Serial.println("[RUNTIME] WELCOME");
    Serial.println("[TEST] Load complete assigned WELCOME scene file by ID");

    const auto result = g_runtime.playWelcome();
    Serial.printf(
        "[RESULT] %s | welcome=%s\n",
        IndicatorSceneRuntime::SceneRuntimeManagerV1::resultName(result),
        g_runtime.assignedWelcomeId());

    if (result != IndicatorSceneRuntime::RuntimeResult::OK) {
        Serial.printf("[FAIL] WELCOME Runtime: %s\n", g_runtime.lastError());
        return;
    }


    Serial.println("[PASS] Assigned WELCOME scene started");
}

static void printAssignmentStatus() {
    Serial.println();
    Serial.println("[ASSIGNMENT STATUS] ESP SOURCE OF TRUTH");
    String activeConfig;
    if (!g_runtime.readActiveAssignmentConfig(activeConfig)) {
        Serial.println("[FAIL] Active assignment.config could not be read/validated");
        return;
    }
    Serial.println("[PASS] Active assignment.config read + validated");
    Serial.println("----- BEGIN ACTIVE ASSIGNMENT -----");
    Serial.print(activeConfig);
    if (!activeConfig.endsWith("\n")) Serial.println();
    Serial.println("------ END ACTIVE ASSIGNMENT ------");
}

static void handleRuntimeEvent(const IndicatorEventManager::Event& event) {
    using IndicatorEventManager::EventType;

    switch (event.type) {
        case EventType::LEFT: {
            Serial.println("[RUNTIME] EVENT_LEFT -> TURN SIGNAL LEFT");
            const auto result = g_runtime.playTurnSignal(IndicatorSceneVM::Side::LEFT);
            Serial.printf("[RESULT] %s | scene=%s | context=%s\n",
                          IndicatorSceneRuntime::SceneRuntimeManagerV1::resultName(result),
                          g_runtime.assignedTurnSignalId(),
                          sideName(g_runtime.activeTurnSignalSide()));
            break;
        }

        case EventType::RIGHT: {
            Serial.println("[RUNTIME] EVENT_RIGHT -> TURN SIGNAL RIGHT");
            const auto result = g_runtime.playTurnSignal(IndicatorSceneVM::Side::RIGHT);
            Serial.printf("[RESULT] %s | scene=%s | context=%s\n",
                          IndicatorSceneRuntime::SceneRuntimeManagerV1::resultName(result),
                          g_runtime.assignedTurnSignalId(),
                          sideName(g_runtime.activeTurnSignalSide()));
            break;
        }

        case EventType::MIDDLE_2_CLICKS: {
            Serial.println("[RUNTIME] EVENT_MIDDLE_2_CLICKS -> STOP ALL INDICATOR");
            const auto result = g_runtime.stopAllIndicator();
            Serial.printf("[RESULT] %s | STOP ALL -> DISCARD PAUSED + BOTH POS 0 SPEED 100\n",
                          IndicatorSceneRuntime::SceneRuntimeManagerV1::resultName(result));
            break;
        }

        case EventType::MIDDLE_1_CLICK: {
            Serial.println("[RUNTIME] EVENT_MIDDLE_1_CLICK -> STOP / RESUME INDICATOR");
            const bool pausedBeforeStop = g_runtime.indicatorPaused();
            const auto result = g_runtime.stopIndicator();
            if (pausedBeforeStop) {
                Serial.printf("[RESULT] %s | INDICATOR STOP -> RESUMED PAUSED SCENE\n",
                              IndicatorSceneRuntime::SceneRuntimeManagerV1::resultName(result));
            } else {
                Serial.printf("[RESULT] %s | INDICATOR STOP -> BOTH POS 0 SPEED 100\n",
                              IndicatorSceneRuntime::SceneRuntimeManagerV1::resultName(result));
            }
            break;
        }

        default: {
            // Any 3-32 click event is resolved through the same assignment
            // lookup. If that click has no assigned scene, Runtime performs
            // an intentional no-op and leaves the current INDICATOR scene
            // untouched.
            if (event.middleClickCount >= IndicatorAssignmentConfig::MULTI_CLICK_MIN &&
                event.middleClickCount <= IndicatorAssignmentConfig::MULTI_CLICK_MAX) {
                const auto result = g_runtime.playClickMappedScene(event.middleClickCount);
                if (result == IndicatorSceneRuntime::RuntimeResult::NO_ASSIGNMENT) {
                    Serial.printf("[RUNTIME] MIDDLE_%u_CLICKS -> no assigned scene; ignored\n",
                                  (unsigned)event.middleClickCount);
                } else {
                    Serial.printf("[RUNTIME] MIDDLE_%u_CLICKS -> assigned scene\n",
                                  (unsigned)event.middleClickCount);
                    Serial.printf("[RESULT] %s | click=%u\n",
                                  IndicatorSceneRuntime::SceneRuntimeManagerV1::resultName(result),
                                  (unsigned)event.middleClickCount);
                }
            } else {
                Serial.println("[EVENT MANAGER] Event normalized; Runtime behaviour not assigned yet.");
            }
            break;
        }
    }
}


static bool getNoPlateLockTimerMs(uint32_t& timerMs) {
    String config;
    if (!g_runtime.readActiveAssignmentConfig(config)) return false;

    int start = 0;
    while (start <= (int)config.length()) {
        int end = config.indexOf('\n', start);
        if (end < 0) end = config.length();

        String line = config.substring(start, end);
        line.replace("\r", "");
        line.trim();

        const int eq = line.indexOf('=');
        if (eq > 0) {
            String key = line.substring(0, eq);
            String value = line.substring(eq + 1);
            key.trim();
            value.trim();

            if (key.equalsIgnoreCase("NO_PLATE LOCK TIMER")) {
                value.toUpperCase();
                if (!value.endsWith("SEC")) return false;
                value.remove(value.length() - 3);
                value.trim();
                if (value.length() == 0) return false;

                for (size_t i = 0; i < value.length(); ++i) {
                    if (!isdigit(static_cast<unsigned char>(value[i]))) return false;
                }

                const unsigned long seconds = strtoul(value.c_str(), nullptr, 10);
                if (seconds == 0 || seconds > 86400UL) return false;
                timerMs = static_cast<uint32_t>(seconds * 1000UL);
                return true;
            }
        }

        if (end >= (int)config.length()) break;
        start = end + 1;
    }

    return false;
}

void setup() {
    NoPlateLockServoV1::begin();
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("========================================");
    Serial.println("INDICATOR V3 - BLE APP TRANSPORT V1 BASELINE");
    Serial.println("DUAL SCENEVMS + INDICATOR REPEAT + EVENT MANAGER V1");
    Serial.println("========================================");
    Serial.println("Event test input: l / r / 1..32 | STATUS / ASSIGNMENT_STATUS");

    Serial.printf(
        "[HEAP] boot free=%u min=%u\n",
        (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getMinFreeHeap());

    if (!g_sceneStorage.begin()) {
        Serial.printf("[FAIL] Scene Storage: %s\n", g_sceneStorage.lastError());
        return;
    }
    Serial.println("[PASS] Scene Storage mounted");

    if (!g_ledHardware.begin()) {
        Serial.println("[FAIL] LED Runtime init");
        return;
    }
    Serial.println("[PASS] LED Runtime initialized");

    if (!g_motorMotion.begin()) {
        Serial.println("[FAIL] Motor Motion init");
        return;
    }
    Serial.println("[PASS] Motor Motion initialized");

    g_noPlateVm = IndicatorSceneVM::createSceneVMV1();
    g_indicatorVm = IndicatorSceneVM::createSceneVMV1();
    if (!g_noPlateVm || !g_indicatorVm) {
        Serial.println("[FAIL] SceneVM allocation (need two independent VMs)");
        return;
    }
    Serial.println("[PASS] Two SceneVM instances allocated");

    if (!g_runtime.begin(g_sceneStorage, *g_noPlateVm, *g_indicatorVm,
                         g_sceneMotionPort, g_ledHardware.runtime())) {
        Serial.printf("[FAIL] Runtime Manager: %s\n", g_runtime.lastError());
        return;
    }
    Serial.println("[PASS] Runtime Manager initialized");

    if (!g_uploadManager.begin(g_sceneStorage)) {
        Serial.printf("[FAIL] Upload Manager: %s\n", g_uploadManager.lastError());
        return;
    }
    g_uploadManager.setRuntimeManager(g_runtime);
    Serial.println("[PASS] App upload manager initialized");

    // Connect APP_COMMANDS to the already-tested firmware subsystems.
    if (!g_appCommands.begin(g_motorMotion, g_runtime, g_sceneStorage, g_uploadManager, handleRuntimeEvent)) {
        Serial.println("[FAIL] App Command Interface init");
        return;
    }
    Serial.println("[PASS] App Command Interface V1 initialized");

    // APP_PROTOCOL sits above APP_COMMANDS and is transport-independent.
    if (!g_appProtocol.begin(g_appCommands)) {
        Serial.println("[FAIL] App Protocol V1 init");
        return;
    }
    Serial.println("[PASS] App Protocol V1 initialized | JSON bench: APPJSON {\"id\":\"1\",\"cmd\":\"LEFT\"}");

    // Start BLE only after the protocol layer is ready.
    // Android talks to the BLE GATT transport; all commands still reach the same
    // APP_PROTOCOL -> APP_COMMANDS -> existing runtime path.
    if (!g_appBleTransport.begin(g_appProtocol)) {
        Serial.println("[FAIL] App BLE Transport V1 init");
        return;
    }

    if (!g_noPlateButton.begin()) {
        Serial.println("[FAIL] NO-PLATE button init");
        return;
    }
    Serial.printf("[PASS] NO-PLATE button initialized GPIO%d | initial=%s\n",
                  IndicatorNoPlate::NO_PLATE_SIDE_BUTTON_PIN,
                  g_noPlateButton.normalSide() ? "NORMAL" : "GRAPHIC");

    // NO-PLATE is autonomous and must start immediately; it does not wait
    // for the homing/WELCOME sequence. Runtime Manager owns its loop/lifetime.
    g_runtime.beginNoPlate(g_noPlateButton);
    Serial.println("[PASS] NO-PLATE autonomous runtime enabled immediately");
    Serial.println("[READY] NO-PLATE GPIO17 HIGH=NORMAL LOW=GRAPHIC");

    if (!g_motorMotion.startHoming(MotionAxis::BOTH)) {
        Serial.println("[FAIL] Homing start");
        return;
    }

    g_motionLights.begin();
    Serial.println("[PASS] Homing started");
    Serial.println("[WAIT] Home both sides; WELCOME will run once after homing.");
}

void loop() {
    NoPlateLockServoV1::update();

    g_motorMotion.update();
    g_motionLights.update(g_motorMotion);
    g_runtime.update(millis());

    // BLE transport owns protocol async completion polling and sends correlated
    // responses/events as BLE notifications. Legacy APP ... Serial commands
    // continue to use APP_COMMANDS_V1 directly through the existing bench bridge.
    g_appBleTransport.update();

    // Preserve the original APP_COMMANDS bench behavior for legacy APP ... commands.
    IndicatorAppCommands::Response appResponse;
    if (g_appCommands.update(appResponse)) {
        Serial.printf("[APP RESPONSE] %s\n", IndicatorAppCommands::AppCommandInterfaceV1::toJson(appResponse).c_str());
    }

    if (!g_homed && bothHomed()) {
        g_homed = true;
        g_ready = true;
        Serial.println("[PASS] Motors homed");

        if (!g_welcomePlayed) {
            g_welcomePlayed = true;
            playWelcome();
        }

        // NO-PLATE was already enabled at boot and continues autonomously.
        Serial.println("[PASS] Motors homed | NO-PLATE autonomous runtime continues");
        Serial.println("[READY] NO-PLATE GPIO17 HIGH=NORMAL LOW=GRAPHIC");

        // Physical controller is deliberately activated only after both
        // motors have completed homing. begin() captures the current button
        // levels, so a button held during boot/homing cannot create a
        // synthetic event.
        g_physicalController.begin();
        Serial.println("[PASS] Physical controller armed | LEFT=D14 MIDDLE=VP/D36 RIGHT=VN/D39");
    }

    // Do not even read the physical controller until homing has completed.
    // This keeps controller input completely outside the boot/homing path.
    if (g_homed) {
        IndicatorEventManager::Event physicalEvent;
        if (g_physicalController.poll(physicalEvent)) {
            Serial.printf("[PHYSICAL EVENT] %s",
                          IndicatorEventManager::EventManagerV1::name(physicalEvent.type));
            if (physicalEvent.middleClickCount != 0) {
                Serial.printf(" | clicks=%u", (unsigned)physicalEvent.middleClickCount);
            }
            Serial.println();
            handleRuntimeEvent(physicalEvent);
        }
    }

    if (millis() - g_lastStatusMs >= 1000) {
        g_lastStatusMs = millis();
        printStatus();
        if (g_homed && !g_runtime.active()) {
            Serial.println("[READY] NO-PLATE autonomous runtime active | LEFT / RIGHT / STATUS");
        }
    }

    if (Serial.available()) {
        String rawCommand = Serial.readStringUntil('\n');
        rawCommand.trim();
        String command = rawCommand;
        command.toUpperCase();

        // APP COMMAND INTERFACE V1 serial bench bridge.
        // This is temporary transport testing: it exercises the real app command
        // interface without needing Wi-Fi/BLE yet.
        // transport for exercising the same command router that Wi-Fi/BLE
        // will call later. Existing l/r/1..32 commands remain untouched.
        // APPJSON is the first real protocol bench transport. The prefix is only
        // a Serial test wrapper; the payload itself is the future app JSON frame.
        if (rawCommand.startsWith("APPJSON ")) {
            // IMPORTANT: use the original frame, not the uppercased legacy command
            // string, so request IDs and future payload text keep their exact bytes.
            const String frame = rawCommand.substring(8);
            const String response = g_appProtocol.processFrame(frame);
            Serial.printf("[APP PROTOCOL RESPONSE] %s\n", response.c_str());
        } else if (command.startsWith("APP ")) {
            String appCommand = command.substring(4);
            appCommand.trim();
            IndicatorAppCommands::Request request;
            bool recognized = true;

            if (appCommand.equalsIgnoreCase("LEFT")) {
                request.command = IndicatorAppCommands::Command::LEFT;
            } else if (appCommand.equalsIgnoreCase("RIGHT")) {
                request.command = IndicatorAppCommands::Command::RIGHT;
            } else if (appCommand.startsWith("MIDDLE ")) {
                String countText = appCommand.substring(7);
                countText.trim();
                const int count = countText.toInt();
                request.command = IndicatorAppCommands::Command::MIDDLE_CLICK;
                request.middleClickCount = static_cast<uint8_t>(count);
            } else if (appCommand.equalsIgnoreCase("SERVICE_EXTEND_LEFT")) {
                request.command = IndicatorAppCommands::Command::SERVICE_EXTEND_LEFT;
            } else if (appCommand.equalsIgnoreCase("SERVICE_EXTEND_RIGHT")) {
                request.command = IndicatorAppCommands::Command::SERVICE_EXTEND_RIGHT;
            } else if (appCommand.equalsIgnoreCase("HOME_LEFT")) {
                request.command = IndicatorAppCommands::Command::HOME_LEFT;
            } else if (appCommand.equalsIgnoreCase("HOME_RIGHT")) {
                request.command = IndicatorAppCommands::Command::HOME_RIGHT;
            } else if (appCommand.equalsIgnoreCase("NO_PLATE_UNLOCK")) {
                request.command = IndicatorAppCommands::Command::NO_PLATE_UNLOCK;
            } else if (appCommand.equalsIgnoreCase("ASSIGNMENT_STATUS")) {
                request.command = IndicatorAppCommands::Command::GET_ASSIGNMENT_STATUS;
            } else {
                recognized = false;
            }

            if (recognized) {
                const auto response = g_appCommands.execute(request);
                Serial.printf("[APP RESPONSE] %s\n", IndicatorAppCommands::AppCommandInterfaceV1::toJson(response).c_str());
            } else {
                Serial.println("[APP] Unknown test command");
            }
        } else if (command.equalsIgnoreCase("UL")) {
            uint32_t lockTimerMs = 0;
            if (!getNoPlateLockTimerMs(lockTimerMs)) {
                Serial.println("[NO_PLATE SERVO] UL rejected | invalid/missing NO_PLATE LOCK TIMER");
            } else if (NoPlateLockServoV1::requestUnlock(lockTimerMs)) {
                Serial.printf("[NO_PLATE SERVO] UL -> UNLOCK PWM=%lu | lock timer=%lu ms\n",
                              (unsigned long)NoPlateLockServoV1::NO_PLATE_UNLOCK_DUTY,
                              (unsigned long)lockTimerMs);
            } else {
                Serial.println("[NO_PLATE SERVO] UL ignored | sequence active or servo attach failed");
            }
        } else if (command.equalsIgnoreCase("STATUS")) {
            printStatus();
        } else if (command.equalsIgnoreCase("ASSIGNMENT_STATUS")) {
            printAssignmentStatus();
        } else {
            IndicatorEventManager::Event event;
            if (g_eventManager.translateSerial(command, event)) {
                Serial.printf("[EVENT] %s", IndicatorEventManager::EventManagerV1::name(event.type));
                if (event.middleClickCount != 0) {
                    Serial.printf(" | clicks=%u", (unsigned)event.middleClickCount);
                }
                handleRuntimeEvent(event);
            } else if (command.length()) {
                Serial.println("[INFO] Event input: l / r / 1..32 | STATUS / ASSIGNMENT_STATUS");
            }
        }
    }
}
