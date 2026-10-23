#include "app_commands_v1.h"

namespace IndicatorAppCommands {

// ============================================================
// APP COMMAND INTERFACE V1 - IMPLEMENTATION
// ============================================================
// This layer is intentionally thin: it translates app requests into
// existing Event Manager, Motor Motion, Servo, Runtime, and Upload APIs.
// Do not put scene/motor business logic here unless a command genuinely
// needs a new app-facing operation.


// ------------------------------------------------------------
// INITIALIZATION
// ------------------------------------------------------------
// Store references to the proven firmware subsystems and reset any
// pending asynchronous operation from a previous session.
bool AppCommandInterfaceV1::begin(
    MotorMotion& motorMotion,
    IndicatorSceneRuntime::SceneRuntimeManagerV1& runtime,
    IndicatorSceneStorage::SceneStorageV1& sceneStorage,
    IndicatorSceneUpload::SceneAssignmentUploadV1& uploadManager,
    EventSink eventSink) {
    motorMotion_ = &motorMotion;
    runtime_ = &runtime;
    sceneStorage_ = &sceneStorage;
    uploadManager_ = &uploadManager;
    eventSink_ = eventSink;
    pendingType_ = PendingType::NONE;
    pendingCommand_ = Command::LEFT_BUTTON;
    pendingAxis_ = MotionAxis::NONE;
    return true;
}

// ------------------------------------------------------------
// RESPONSE HELPERS
// ------------------------------------------------------------
Response AppCommandInterfaceV1::ok(Command command) const {
    Response response;
    response.command = command;
    response.result = Result::OK;
    return response;
}

Response AppCommandInterfaceV1::started(Command command) {
    Response response;
    response.command = command;
    response.result = Result::STARTED;
    return response;
}

Response AppCommandInterfaceV1::error(Command command, ErrorCode code) const {
    Response response;
    response.command = command;
    response.result = Result::ERROR;
    response.error = code;
    return response;
}

// ------------------------------------------------------------
// EVENT DISPATCH
// LEFT/RIGHT and middle-click commands become normal Event Manager events.
// This keeps the app and physical controller on the same runtime path.
// ------------------------------------------------------------
Response AppCommandInterfaceV1::dispatchEvent(
    Command command,
    IndicatorEventManager::EventType type,
    uint8_t middleClicks) const {
    if (!eventSink_) return error(command, ErrorCode::BUSY);

    IndicatorEventManager::Event event{};
    event.type = type;
    event.middleClickCount = middleClicks;
    eventSink_(event);
    return ok(command);
}

// Translate low-level motion start failure into the app-facing error vocabulary.
ErrorCode AppCommandInterfaceV1::motionStartError(MotionAxis axis) const {
    if (!motorMotion_) return ErrorCode::MOTOR_FAULT;

    const MotionStatus left = motorMotion_->status(MotionAxis::LEFT);
    const MotionStatus right = motorMotion_->status(MotionAxis::RIGHT);
    const MotionStatus& status = axis == MotionAxis::LEFT ? left : right;

    if (!status.homed) return ErrorCode::NOT_HOMED;
    if (status.state == MotionState::FAULT || status.fault != MotionFault::NONE) {
        return ErrorCode::MOTOR_FAULT;
    }
    return ErrorCode::MOTOR_FAULT;
}

// ------------------------------------------------------------
// SERVICE / HOME COMMANDS
// ------------------------------------------------------------
// EXTEND uses the deliberately slower service speed.
// HOME is different: it MUST call the same physical homing routine used at boot.
// Therefore HOME never assumes a fixed reverse distance and can be requested
// repeatedly even when the axis is already homed.
Response AppCommandInterfaceV1::startService(Command command,
                                              MotionAxis axis,
                                              bool home) {
    if (pendingType_ != PendingType::NONE) return error(command, ErrorCode::BUSY);
    if (!motorMotion_) return error(command, ErrorCode::MOTOR_FAULT);

    const MotionStatus left = motorMotion_->status(MotionAxis::LEFT);
    const MotionStatus right = motorMotion_->status(MotionAxis::RIGHT);
    const MotionStatus& status = axis == MotionAxis::LEFT ? left : right;

    // APP HOME must be the exact normal homing operation used at boot.
    // It is intentionally valid even when the axis is already homed so the
    // app can request HOME repeatedly. startHoming() itself resets the
    // homing state/fault and performs the normal seek -> release sequence.
    if (home) {
        const bool startedMove = motorMotion_->home(axis);
        if (!startedMove) return error(command, motionStartError(axis));

        pendingType_ = PendingType::SERVICE;
        pendingCommand_ = command;
        pendingAxis_ = axis;
        return started(command);
    }

    if (!status.homed) return error(command, ErrorCode::NOT_HOMED);
    if (status.state == MotionState::FAULT || status.fault != MotionFault::NONE) {
        return error(command, ErrorCode::MOTOR_FAULT);
    }

    const bool startedMove = motorMotion_->serviceExtend(axis, SERVICE_SPEED_PERCENT);

    if (!startedMove) return error(command, motionStartError(axis));

    pendingType_ = PendingType::SERVICE;
    pendingCommand_ = command;
    pendingAxis_ = axis;
    return started(command);
}

// ------------------------------------------------------------
// NO-PLATE CONFIG READER
// ------------------------------------------------------------
// Read the active assignment config instead of hard-coding the servo timer.
// ------------------------------------------------------------
bool AppCommandInterfaceV1::readNoPlateLockTimerMs(const String& config,
                                                    uint32_t& timerMs) {
    int start = 0;
    while (start <= static_cast<int>(config.length())) {
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

        if (end >= static_cast<int>(config.length())) break;
        start = end + 1;
    }
    return false;
}

// Start the existing non-blocking NO-PLATE servo sequence using the
// timer selected by the active assignment configuration.
Response AppCommandInterfaceV1::startNoPlateUnlock(Command command) {
    if (pendingType_ != PendingType::NONE) return error(command, ErrorCode::BUSY);
    if (!runtime_) return error(command, ErrorCode::NO_PLATE_ERROR);

    String config;
    if (!runtime_->readActiveAssignmentConfig(config)) {
        return error(command, ErrorCode::CONFIG_INVALID);
    }

    uint32_t lockTimerMs = 0;
    if (!readNoPlateLockTimerMs(config, lockTimerMs)) {
        return error(command, ErrorCode::CONFIG_INVALID);
    }

    if (!NoPlateLockServoV1::requestUnlock(lockTimerMs)) {
        return error(command, ErrorCode::NO_PLATE_ERROR);
    }

    pendingType_ = PendingType::NO_PLATE_UNLOCK;
    pendingCommand_ = command;
    pendingAxis_ = MotionAxis::NONE;
    return started(command);
}

// ------------------------------------------------------------
// MAIN COMMAND ROUTER
// ------------------------------------------------------------
// This switch is the single entry point for app commands.
// Keep it boring: validate, translate, call the existing subsystem, respond.
Response AppCommandInterfaceV1::execute(const Request& request) {
    switch (request.command) {
        case Command::LEFT_BUTTON:
            return dispatchEvent(request.command,
                                 IndicatorEventManager::EventType::LEFT);

        case Command::RIGHT_BUTTON:
            return dispatchEvent(request.command,
                                 IndicatorEventManager::EventType::RIGHT);

        case Command::MIDDLE_BUTTON: {
            if (request.middleClickCount < 1 ||
                request.middleClickCount > MAX_MIDDLE_CLICKS) {
                return error(request.command, ErrorCode::INVALID_PAYLOAD);
            }

            const auto type = static_cast<IndicatorEventManager::EventType>(
                static_cast<uint8_t>(IndicatorEventManager::EventType::MIDDLE_1_CLICK) +
                static_cast<uint8_t>(request.middleClickCount - 1));
            return dispatchEvent(request.command, type, request.middleClickCount);
        }

        case Command::SERVICE_EXTEND_LEFT:
            return startService(request.command, MotionAxis::LEFT, false);

        case Command::SERVICE_EXTEND_RIGHT:
            return startService(request.command, MotionAxis::RIGHT, false);

        case Command::HOME_LEFT:
            return startService(request.command, MotionAxis::LEFT, true);

        case Command::HOME_RIGHT:
            return startService(request.command, MotionAxis::RIGHT, true);

        case Command::NO_PLATE_UNLOCK:
            return startNoPlateUnlock(request.command);

        case Command::UPLOAD_PACKAGE:
            return error(request.command, ErrorCode::INVALID_PAYLOAD);

        case Command::ASSIGNMENT_STATUS: {
            if (!runtime_) return error(request.command, ErrorCode::CONFIG_INVALID);

            String config;
            if (!runtime_->readActiveAssignmentConfig(config)) {
                return error(request.command, ErrorCode::CONFIG_INVALID);
            }

            Response response = ok(request.command);
            response.data = config;
            return response;
        }
    }

    return error(request.command, ErrorCode::INVALID_COMMAND);
}

// ------------------------------------------------------------
// PACKAGE UPLOAD
// ------------------------------------------------------------
// Scene/config installation remains owned by Upload Manager.
// This layer only translates its result into the app error vocabulary.
Response AppCommandInterfaceV1::uploadPackage(const UploadPackage& package) {
    if (!uploadManager_) return error(Command::UPLOAD_PACKAGE, ErrorCode::UPLOAD_FAILED);
    if (!package.assignmentConfig || package.assignmentConfigLength == 0) {
        return error(Command::UPLOAD_PACKAGE, ErrorCode::INVALID_PAYLOAD);
    }
    if (package.sceneCount > IndicatorSceneUpload::MAX_UPLOAD_SCENES) {
        return error(Command::UPLOAD_PACKAGE, ErrorCode::INVALID_PAYLOAD);
    }

    const IndicatorSceneUpload::UploadResult result = uploadManager_->install(
        package.scenes,
        package.sceneCount,
        package.assignmentConfig,
        package.assignmentConfigLength);

    if (result == IndicatorSceneUpload::UploadResult::INVALID_CONFIG) {
        return error(Command::UPLOAD_PACKAGE, ErrorCode::CONFIG_INVALID);
    }

    if (result == IndicatorSceneUpload::UploadResult::MISSING_SCENE_REFERENCE) {
        return error(Command::UPLOAD_PACKAGE, ErrorCode::CONFIG_INVALID);
    }

    if (result == IndicatorSceneUpload::UploadResult::SCENE_INSTALL_FAILED ||
        result == IndicatorSceneUpload::UploadResult::DUPLICATE_UPLOAD_ID ||
        result == IndicatorSceneUpload::UploadResult::TOO_MANY_SCENES ||
        result == IndicatorSceneUpload::UploadResult::VERIFY_FAILED) {
        return error(Command::UPLOAD_PACKAGE, ErrorCode::SCENE_INVALID);
    }

    if (result == IndicatorSceneUpload::UploadResult::CONFIG_WRITE_FAILED ||
        result == IndicatorSceneUpload::UploadResult::NOT_READY) {
        return error(Command::UPLOAD_PACKAGE, ErrorCode::UPLOAD_FAILED);
    }

    Response response;
    response.command = Command::UPLOAD_PACKAGE;
    response.result = Result::COMPLETE;
    response.data = IndicatorSceneUpload::SceneAssignmentUploadV1::resultName(result);
    return response;
}

// ------------------------------------------------------------
// ASYNCHRONOUS COMPLETION
// ------------------------------------------------------------
// Service/home commands return STARTED immediately. These functions watch
// the already-running hardware operation and generate the final response.
bool AppCommandInterfaceV1::serviceFinished(Response& out) {
    if (!motorMotion_) {
        out = error(pendingCommand_, ErrorCode::MOTOR_FAULT);
        return true;
    }

    const MotionStatus status = motorMotion_->status(pendingAxis_);
    if (status.state == MotionState::FAULT || status.fault != MotionFault::NONE) {
        out = error(pendingCommand_, ErrorCode::MOTOR_FAULT);
        pendingType_ = PendingType::NONE;
        pendingAxis_ = MotionAxis::NONE;
        return true;
    }

    if (!status.serviceMoveActive && status.state == MotionState::AT_TARGET) {
        out = Response{};
        out.command = pendingCommand_;
        out.result = Result::COMPLETED;
        pendingType_ = PendingType::NONE;
        pendingAxis_ = MotionAxis::NONE;
        return true;
    }

    return false;
}

// Watch the existing non-blocking servo state until it reaches LOCKED.
bool AppCommandInterfaceV1::noPlateFinished(Response& out) {
    const NoPlateLockServoV1::State state = NoPlateLockServoV1::state();

    if (state == NoPlateLockServoV1::State::LOCKED) {
        out = Response{};
        out.command = pendingCommand_;
        out.result = Result::COMPLETED;
        pendingType_ = PendingType::NONE;
        pendingAxis_ = MotionAxis::NONE;
        return true;
    }

    return false;
}

// Called from loop() so app commands never block the main firmware loop.
bool AppCommandInterfaceV1::update(Response& out) {
    out = Response{};
    if (pendingType_ == PendingType::NONE) return false;

    if (pendingType_ == PendingType::SERVICE) return serviceFinished(out);
    if (pendingType_ == PendingType::NO_PLATE_UNLOCK) return noPlateFinished(out);
    return false;
}

// ------------------------------------------------------------
// PROTOCOL SERIALIZATION
// ------------------------------------------------------------
const char* AppCommandInterfaceV1::commandName(Command command) {
    switch (command) {
        case Command::LEFT_BUTTON: return "LEFT_BUTTON";
        case Command::MIDDLE_BUTTON: return "MIDDLE_BUTTON";
        case Command::RIGHT_BUTTON: return "RIGHT_BUTTON";
        case Command::SERVICE_EXTEND_LEFT: return "SERVICE_LEFT_EXTEND";
        case Command::SERVICE_EXTEND_RIGHT: return "SERVICE_RIGHT_EXTEND";
        case Command::HOME_LEFT: return "HOME_LEFT";
        case Command::HOME_RIGHT: return "HOME_RIGHT";
        case Command::NO_PLATE_UNLOCK: return "NO_PLATE_UNLOCK";
        case Command::UPLOAD_PACKAGE: return "UPLOAD_PACKAGE";
        case Command::ASSIGNMENT_STATUS: return "ASSIGNMENT_STATUS";
    }
    return "UNKNOWN";
}

const char* AppCommandInterfaceV1::resultName(Result result) {
    switch (result) {
        case Result::OK: return "OK";
        case Result::STARTED: return "STARTED";
        case Result::COMPLETED: return "COMPLETED";
        case Result::COMPLETE: return "COMPLETE";
        case Result::ERROR: return "ERROR";
    }
    return "ERROR";
}

const char* AppCommandInterfaceV1::errorName(ErrorCode error) {
    switch (error) {
        case ErrorCode::NONE: return "";
        case ErrorCode::INVALID_COMMAND: return "INVALID_COMMAND";
        case ErrorCode::INVALID_PAYLOAD: return "INVALID_PAYLOAD";
        case ErrorCode::BUSY: return "BUSY";
        case ErrorCode::NOT_HOMED: return "NOT_HOMED";
        case ErrorCode::MOTOR_FAULT: return "MOTOR_FAULT";
        case ErrorCode::NO_PLATE_ERROR: return "NO_PLATE_ERROR";
        case ErrorCode::SCENE_INVALID: return "SCENE_INVALID";
        case ErrorCode::CONFIG_INVALID: return "CONFIG_INVALID";
        case ErrorCode::UPLOAD_FAILED: return "UPLOAD_FAILED";
    }
    return "INVALID_COMMAND";
}

// Escape assignment.config text so it can safely travel inside JSON data.
String AppCommandInterfaceV1::escapeJson(const String& input) {
    String out;
    out.reserve(input.length() + 16);
    for (size_t i = 0; i < input.length(); ++i) {
        const char c = input[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

// Final response formatter used by the current Serial bench bridge and
// intended to be reused unchanged by the future Wi-Fi/BLE transport.
String AppCommandInterfaceV1::toJson(const Response& response) {
    String out;
    out.reserve(96 + response.data.length());
    out += "{\"cmd\":\"";
    out += commandName(response.command);
    out += "\",\"result\":\"";
    out += resultName(response.result);
    out += "\"";

    if (response.error != ErrorCode::NONE) {
        out += ",\"error\":\"";
        out += errorName(response.error);
        out += "\"";
    }

    if (response.data.length() > 0) {
        out += ",\"data\":\"";
        out += escapeJson(response.data);
        out += "\"";
    }

    out += "}";
    return out;
}

} // namespace IndicatorAppCommands
