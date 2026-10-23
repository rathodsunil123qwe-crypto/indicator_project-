#include "app_protocol_v1.h"

namespace IndicatorAppProtocol {

// ------------------------------------------------------------
// INITIALIZATION
// ------------------------------------------------------------
bool AppProtocolV1::begin(IndicatorAppCommands::AppCommandInterfaceV1& commands) {
    commands_ = &commands;
    pendingRequestId_ = "";
    return true;
}

// ------------------------------------------------------------
// SMALL JSON HELPERS
// ------------------------------------------------------------
// V1 deliberately uses a tiny parser because the protocol only needs a small,
// fixed command envelope. This avoids adding a third-party JSON dependency to
// the proven firmware baseline.
bool AppProtocolV1::extractString(const String& json, const char* key, String& value) {
    String needle = "\"";
    needle += key;
    needle += "\"";

    const int keyPos = json.indexOf(needle);
    if (keyPos < 0) return false;

    int colon = json.indexOf(':', keyPos + needle.length());
    if (colon < 0) return false;

    int quote = colon + 1;
    while (quote < static_cast<int>(json.length()) &&
           (json[quote] == ' ' || json[quote] == '\t' || json[quote] == '\r' || json[quote] == '\n')) {
        ++quote;
    }
    if (quote >= static_cast<int>(json.length()) || json[quote] != '"') return false;

    ++quote;
    String out;
    bool escaped = false;

    for (int i = quote; i < static_cast<int>(json.length()); ++i) {
        const char c = json[i];
        if (escaped) {
            switch (c) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: return false;
            }
            escaped = false;
            continue;
        }

        if (c == '\\') {
            escaped = true;
            continue;
        }

        if (c == '"') {
            value = out;
            return true;
        }

        out += c;
    }

    return false;
}

bool AppProtocolV1::extractUnsigned(const String& json, const char* key, uint8_t& value) {
    String needle = "\"";
    needle += key;
    needle += "\"";

    const int keyPos = json.indexOf(needle);
    if (keyPos < 0) return false;

    int colon = json.indexOf(':', keyPos + needle.length());
    if (colon < 0) return false;

    int pos = colon + 1;
    while (pos < static_cast<int>(json.length()) &&
           (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }

    if (pos >= static_cast<int>(json.length()) || !isdigit(static_cast<unsigned char>(json[pos]))) {
        return false;
    }

    unsigned long number = 0;
    while (pos < static_cast<int>(json.length()) &&
           isdigit(static_cast<unsigned char>(json[pos]))) {
        number = number * 10UL + static_cast<unsigned long>(json[pos] - '0');
        if (number > 255UL) return false;
        ++pos;
    }

    value = static_cast<uint8_t>(number);
    return true;
}

bool AppProtocolV1::hasOnlyWhitespaceAfter(const String& json, int index) {
    for (int i = index; i < static_cast<int>(json.length()); ++i) {
        if (json[i] != ' ' && json[i] != '\t' && json[i] != '\r' && json[i] != '\n') return false;
    }
    return true;
}

// ------------------------------------------------------------
// COMMAND NAME MAPPING
// ------------------------------------------------------------
// Protocol names are intentionally stable and human-readable. They are not
// tied to the internal enum names used by the firmware implementation.
bool AppProtocolV1::commandFromName(
    const String& name,
    IndicatorAppCommands::Command& command) {
    // Canonical app-facing names.
    if (name.equalsIgnoreCase("LEFT_BUTTON")) {
        command = IndicatorAppCommands::Command::LEFT_BUTTON;
        return true;
    }
    if (name.equalsIgnoreCase("RIGHT_BUTTON")) {
        command = IndicatorAppCommands::Command::RIGHT_BUTTON;
        return true;
    }
    if (name.equalsIgnoreCase("MIDDLE_BUTTON")) {
        command = IndicatorAppCommands::Command::MIDDLE_BUTTON;
        return true;
    }
    if (name.equalsIgnoreCase("SERVICE_LEFT_EXTEND")) {
        command = IndicatorAppCommands::Command::SERVICE_EXTEND_LEFT;
        return true;
    }
    if (name.equalsIgnoreCase("SERVICE_RIGHT_EXTEND")) {
        command = IndicatorAppCommands::Command::SERVICE_EXTEND_RIGHT;
        return true;
    }
    if (name.equalsIgnoreCase("HOME_LEFT")) {
        command = IndicatorAppCommands::Command::HOME_LEFT;
        return true;
    }
    if (name.equalsIgnoreCase("HOME_RIGHT")) {
        command = IndicatorAppCommands::Command::HOME_RIGHT;
        return true;
    }
    if (name.equalsIgnoreCase("NO_PLATE_UNLOCK")) {
        command = IndicatorAppCommands::Command::NO_PLATE_UNLOCK;
        return true;
    }
    if (name.equalsIgnoreCase("ASSIGNMENT_STATUS")) {
        command = IndicatorAppCommands::Command::ASSIGNMENT_STATUS;
        return true;
    }
    if (name.equalsIgnoreCase("UPLOAD_PACKAGE")) {
        command = IndicatorAppCommands::Command::UPLOAD_PACKAGE;
        return true;
    }

    // Backward-compatible request names from APP TRANSPORT V1.
    if (name.equalsIgnoreCase("LEFT")) {
        command = IndicatorAppCommands::Command::LEFT_BUTTON;
        return true;
    }
    if (name.equalsIgnoreCase("RIGHT")) {
        command = IndicatorAppCommands::Command::RIGHT_BUTTON;
        return true;
    }
    if (name.equalsIgnoreCase("MIDDLE_CLICK")) {
        command = IndicatorAppCommands::Command::MIDDLE_BUTTON;
        return true;
    }
    return false;
}

// ------------------------------------------------------------
// JSON OUTPUT
// ------------------------------------------------------------
String AppProtocolV1::escapeJson(const String& input) {
    String out;
    out.reserve(input.length() + 16);

    for (size_t i = 0; i < input.length(); ++i) {
        switch (input[i]) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += input[i]; break;
        }
    }
    return out;
}

String AppProtocolV1::makeError(const String& requestId,
                                const char* error,
                                const char* detail) {
    String out;
    out.reserve(128);
    out += "{\"id\":\"";
    out += escapeJson(requestId);
    out += "\",\"result\":\"ERROR\",\"error\":\"";
    out += error;
    out += "\"";
    if (detail && detail[0] != '\0') {
        out += ",\"detail\":\"";
        out += escapeJson(String(detail));
        out += "\"";
    }
    out += "}";
    return out;
}

String AppProtocolV1::makeResponse(
    const String& requestId,
    const IndicatorAppCommands::Response& response) {
    // APP_COMMANDS_V1 already owns the stable application result vocabulary.
    // Protocol V1 simply adds the request correlation ID around that response.
    String out;
    out.reserve(128 + response.data.length());
    out += "{\"id\":\"";
    out += escapeJson(requestId);
    out += "\",\"cmd\":\"";
    out += IndicatorAppCommands::AppCommandInterfaceV1::commandName(response.command);
    out += "\",\"result\":\"";
    out += IndicatorAppCommands::AppCommandInterfaceV1::resultName(response.result);
    out += "\"";

    if (response.error != IndicatorAppCommands::ErrorCode::NONE) {
        out += ",\"error\":\"";
        out += IndicatorAppCommands::AppCommandInterfaceV1::errorName(response.error);
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

// ------------------------------------------------------------
// REQUEST PROCESSING
// ------------------------------------------------------------
String AppProtocolV1::processFrame(const String& frame) {
    if (!commands_) return makeError("", "NOT_READY");
    if (frame.length() == 0 || frame.length() > MAX_FRAME_LENGTH) {
        return makeError("", "INVALID_PAYLOAD", "FRAME_LENGTH");
    }

    String json = frame;
    json.trim();
    if (json.length() < 2 || json[0] != '{' || json[json.length() - 1] != '}') {
        return makeError("", "INVALID_PAYLOAD", "EXPECTED_JSON_OBJECT");
    }

    // The V1 envelope requires an ID and command. Keeping IDs mandatory makes
    // future asynchronous Wi-Fi/BLE responses safely match their request.
    String requestId;
    if (!extractString(json, "id", requestId) ||
        requestId.length() == 0 || requestId.length() > MAX_REQUEST_ID_LENGTH) {
        return makeError("", "INVALID_PAYLOAD", "MISSING_OR_INVALID_ID");
    }

    String commandName;
    if (!extractString(json, "cmd", commandName)) {
        return makeError(requestId, "INVALID_PAYLOAD", "MISSING_CMD");
    }

    IndicatorAppCommands::Command command;
    if (!commandFromName(commandName, command)) {
        return makeError(requestId, "INVALID_COMMAND");
    }

    IndicatorAppCommands::Request request;
    request.command = command;

    if (command == IndicatorAppCommands::Command::MIDDLE_BUTTON) {
        if (!extractUnsigned(json, "clicks", request.middleClickCount) ||
            request.middleClickCount < 1 ||
            request.middleClickCount > IndicatorAppCommands::MAX_MIDDLE_CLICKS) {
            return makeError(requestId, "INVALID_PAYLOAD", "CLICKS_MUST_BE_1_TO_32");
        }
    }

    // V1 rejects an upload through the scalar command envelope. The typed
    // UploadPackage API remains the dedicated boundary for the later upload
    // transport, so we do not invent a fragile JSON scene-file format here.
    if (command == IndicatorAppCommands::Command::UPLOAD_PACKAGE) {
        return makeError(requestId, "INVALID_PAYLOAD", "USE_UPLOAD_PACKAGE_API");
    }

    const IndicatorAppCommands::Response response = commands_->execute(request);

    if (response.result == IndicatorAppCommands::Result::STARTED) {
        // Only one async operation exists in APP_COMMANDS_V1 at a time.
        pendingRequestId_ = requestId;
    }

    return makeResponse(requestId, response);
}

// ------------------------------------------------------------
// TYPED PACKAGE UPLOAD
// ------------------------------------------------------------
// Upload data arrives through the dedicated transport endpoint rather than
// the small scalar JSON command frame. APP_PROTOCOL still owns the response
// envelope and request correlation.
String AppProtocolV1::uploadPackage(
    const String& requestId,
    const IndicatorAppCommands::UploadPackage& package) {
    if (!commands_) return makeError(requestId, "NOT_READY");
    if (requestId.length() == 0 || requestId.length() > MAX_REQUEST_ID_LENGTH) {
        return makeError(requestId, "INVALID_PAYLOAD", "MISSING_OR_INVALID_ID");
    }

    const IndicatorAppCommands::Response response = commands_->uploadPackage(package);
    return makeResponse(requestId, response);
}

// ------------------------------------------------------------
// ASYNCHRONOUS RESPONSE CORRELATION
// ------------------------------------------------------------
bool AppProtocolV1::update(String& out) {
    out = "";
    if (!commands_ || pendingRequestId_.length() == 0) return false;

    IndicatorAppCommands::Response response;
    if (!commands_->update(response)) return false;

    out = makeResponse(pendingRequestId_, response);
    pendingRequestId_ = "";
    return true;
}

} // namespace IndicatorAppProtocol
