#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "../APP_COMMANDS/app_commands_v1.h"

namespace IndicatorAppProtocol {

// ============================================================
// APP PROTOCOL V1
// ============================================================
// Transport-independent JSON protocol sitting ABOVE APP_COMMANDS_V1.
// Wi-Fi, BLE, or Serial only need to deliver one complete JSON frame here.
// This module validates/parses the frame and then calls APP_COMMANDS_V1.
// It must never call Motion, SceneVM, Runtime Manager, or Servo directly.
// ============================================================

static constexpr size_t MAX_FRAME_LENGTH = 512;
static constexpr size_t MAX_REQUEST_ID_LENGTH = 32;

class AppProtocolV1 {
public:
    bool begin(IndicatorAppCommands::AppCommandInterfaceV1& commands);

    // Process one complete JSON request and return one complete JSON response.
    // A transport should call this only after it has received a full frame.
    String processFrame(const String& frame);

    // Poll asynchronous APP_COMMANDS operations. If a service/home/servo
    // operation finishes, this returns its correlated JSON response.
    bool update(String& out);

    // Typed package-upload boundary used by APP_TRANSPORT. The upload payload
    // is intentionally kept out of the scalar 512-byte command frame.
    String uploadPackage(const String& requestId,
                         const IndicatorAppCommands::UploadPackage& package);

private:
    IndicatorAppCommands::AppCommandInterfaceV1* commands_ = nullptr;

    // APP_COMMANDS_V1 allows one asynchronous operation at a time.
    // Keep its request ID here so COMPLETED/ERROR can return to the same app request.
    String pendingRequestId_;

    static bool extractString(const String& json, const char* key, String& value);
    static bool extractUnsigned(const String& json, const char* key, uint8_t& value);
    static bool hasOnlyWhitespaceAfter(const String& json, int index);

    static bool commandFromName(const String& name,
                                IndicatorAppCommands::Command& command);

    static String makeError(const String& requestId,
                            const char* error,
                            const char* detail = nullptr);
    static String makeResponse(const String& requestId,
                               const IndicatorAppCommands::Response& response);

    static String escapeJson(const String& input);
};

} // namespace IndicatorAppProtocol
