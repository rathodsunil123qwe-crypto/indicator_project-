#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "../EVENT_MANAGER/event_manager_v1.h"
#include "../MOTOR_MOTION/motor_motion.h"
#include "../NO_PLATE/no_plate_lock_servo_v1.h"
#include "../RUNTIME_MANAGER/scene_runtime_manager_v1.h"
#include "../SCENE_STORAGE/scene_storage_v1.h"
#include "../UPLOAD_MANAGER/scene_assignment_upload_v1.h"

namespace IndicatorAppCommands {

static constexpr uint8_t MAX_MIDDLE_CLICKS = 32;
static constexpr uint8_t SERVICE_SPEED_PERCENT = 50; // Slower service EXTEND speed; HOME uses normal homing PWM.

// ============================================================
// APP COMMAND INTERFACE V1
// ============================================================
// Transport-independent application command contract. Wi-Fi/BLE transport
// code should translate its incoming frame into Request and call execute().
// This module routes only to existing, tested firmware subsystems.
// ============================================================

// Commands exposed to the future Android app.
// Keep these transport-independent: Wi-Fi/BLE/Serial should all map to this same enum.
enum class Command : uint8_t {
    // Canonical app-facing command names.
    LEFT_BUTTON = 0,
    MIDDLE_BUTTON,
    RIGHT_BUTTON,
    SERVICE_EXTEND_LEFT,
    SERVICE_EXTEND_RIGHT,
    HOME_LEFT,
    HOME_RIGHT,
    NO_PLATE_UNLOCK,
    UPLOAD_PACKAGE,
    ASSIGNMENT_STATUS,

    // Backward-compatible internal aliases. Existing firmware code can keep
    // using the old enum names without changing locked runtime/serial code.
    LEFT = LEFT_BUTTON,
    MIDDLE_CLICK = MIDDLE_BUTTON,
    RIGHT = RIGHT_BUTTON,
    GET_ASSIGNMENT_STATUS = ASSIGNMENT_STATUS
};

// Immediate/asynchronous result states returned to the app.
// STARTED means the operation continues in update(); COMPLETE/COMPLETED means it finished.
enum class Result : uint8_t {
    OK = 0,
    STARTED,
    COMPLETED,
    COMPLETE,
    ERROR
};

// Stable application-level error vocabulary.
// These hide the internal firmware implementation from the future app.
enum class ErrorCode : uint8_t {
    NONE = 0,
    INVALID_COMMAND,
    INVALID_PAYLOAD,
    BUSY,
    NOT_HOMED,
    MOTOR_FAULT,
    NO_PLATE_ERROR,
    SCENE_INVALID,
    CONFIG_INVALID,
    UPLOAD_FAILED
};

// Small command request. Scene uploads use the separate UploadPackage structure below.
struct Request {
    Command command = Command::LEFT_BUTTON;
    uint8_t middleClickCount = 0;
};

// Upload payload kept separate because scenes/config are variable-length data.
struct UploadPackage {
    const IndicatorSceneUpload::UploadSceneText* scenes = nullptr;
    size_t sceneCount = 0;
    const char* assignmentConfig = nullptr;
    size_t assignmentConfigLength = 0;
};

// One normalized response format for Serial, Wi-Fi, BLE, or any future transport.
struct Response {
    Command command = Command::LEFT_BUTTON;
    Result result = Result::ERROR;
    ErrorCode error = ErrorCode::NONE;
    String data;

    bool isError() const { return result == Result::ERROR; }
};

using EventSink = void (*)(const IndicatorEventManager::Event& event);

class AppCommandInterfaceV1 {
public:
    // Connect the command layer to already-tested firmware subsystems.
    // This function does not create or replace any motor/scene/servo logic.
    bool begin(MotorMotion& motorMotion,
               IndicatorSceneRuntime::SceneRuntimeManagerV1& runtime,
               IndicatorSceneStorage::SceneStorageV1& sceneStorage,
               IndicatorSceneUpload::SceneAssignmentUploadV1& uploadManager,
               EventSink eventSink);

    // Execute an application command. Long-running commands return STARTED
    // here and produce COMPLETED/ERROR from update().
    // Route one command. Fast commands finish here; long operations return STARTED.
    Response execute(const Request& request);

    // Upload is deliberately a separate typed API because scene/config bytes
    // are transport payloads, not a small command scalar.
    Response uploadPackage(const UploadPackage& package);

    // Advance pending service/NO-PLATE operations. Returns true when a new
    // asynchronous response is available in out.
    // Poll asynchronous service/home/servo operations without blocking the main loop.
    bool update(Response& out);

    static const char* commandName(Command command);
    static const char* resultName(Result result);
    static const char* errorName(ErrorCode error);

    // Convert the internal response to the compact JSON contract used by future transports. for the future Wi-Fi/BLE adapter.
    static String toJson(const Response& response);

private:
    enum class PendingType : uint8_t {
        NONE = 0,
        SERVICE,
        NO_PLATE_UNLOCK
    };

    MotorMotion* motorMotion_ = nullptr;
    IndicatorSceneRuntime::SceneRuntimeManagerV1* runtime_ = nullptr;
    IndicatorSceneStorage::SceneStorageV1* sceneStorage_ = nullptr;
    IndicatorSceneUpload::SceneAssignmentUploadV1* uploadManager_ = nullptr;
    EventSink eventSink_ = nullptr;

    PendingType pendingType_ = PendingType::NONE;
    Command pendingCommand_ = Command::LEFT_BUTTON;
    MotionAxis pendingAxis_ = MotionAxis::NONE;

    Response ok(Command command) const;
    Response started(Command command);
    Response error(Command command, ErrorCode code) const;

    Response dispatchEvent(Command command, IndicatorEventManager::EventType type,
                           uint8_t middleClicks = 0) const;
    Response startService(Command command, MotionAxis axis, bool home);
    Response startNoPlateUnlock(Command command);

    bool serviceFinished(Response& out);
    bool noPlateFinished(Response& out);

    ErrorCode motionStartError(MotionAxis axis) const;
    static String escapeJson(const String& input);
    static bool readNoPlateLockTimerMs(const String& config, uint32_t& timerMs);
};

} // namespace IndicatorAppCommands
