#include "app_transport_v1.h"

namespace IndicatorAppTransport {

// ------------------------------------------------------------
// INITIALIZATION
// ------------------------------------------------------------
bool AppTransportV1::begin(IndicatorAppProtocol::AppProtocolV1& protocol) {
    protocol_ = &protocol;
    queuedResponse_ = "";
    resetUploadState();

    // V1 deliberately starts its own Wi-Fi access point. This makes the first
    // Android integration deterministic: no router credentials are required.
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD)) {
        Serial.println("[FAIL] App Transport V1 Wi-Fi AP start");
        return false;
    }

    // HTTP endpoint carrying the already-tested APP_PROTOCOL JSON envelope.
    server_.on("/api/command", HTTP_POST, [this]() { handleCommand(); });

    // Async commands such as HOME and SERVICE_EXTEND return STARTED first.
    // Android polls this endpoint for the correlated COMPLETED/ERROR response.
    server_.on("/api/events", HTTP_GET, [this]() { handleEvents(); });

    // Small connection/diagnostic endpoint for the future Android app.
    server_.on("/api/health", HTTP_GET, [this]() { handleHealth(); });

    // Dedicated typed package upload. Multipart fields are intentionally
    // simple: repeat `scene` for scene text and provide exactly one `config`
    // field for assignment.config. Filenames are never consulted.
    server_.on("/api/upload", HTTP_POST,
               [this]() { handleUploadComplete(); },
               [this]() { handleUploadData(); });

    server_.onNotFound([this]() { handleNotFound(); });
    server_.begin();
    running_ = true;

    Serial.printf("[PASS] App Transport V1 started | SSID=%s | IP=%s | HTTP=%u\n",
                  WIFI_AP_SSID,
                  WiFi.softAPIP().toString().c_str(),
                  (unsigned)HTTP_PORT);
    Serial.println("[READY] POST /api/command | GET /api/events | GET /api/health | POST /api/upload");
    return true;
}

// ------------------------------------------------------------
// MAIN TRANSPORT UPDATE
// ------------------------------------------------------------
void AppTransportV1::update() {
    if (!running_ || !protocol_) return;

    // Keep HTTP handling non-blocking from the firmware architecture's point
    // of view: the existing main loop continues running motor/scene updates.
    server_.handleClient();

    // APP_PROTOCOL owns async command completion. The transport only captures
    // the resulting frame and makes it available through /api/events.
    String response;
    if (protocol_->update(response)) {
        queuedResponse_ = response;
        Serial.printf("[APP PROTOCOL RESPONSE] %s\n", response.c_str());
    }
}

// ------------------------------------------------------------
// HTTP JSON COMMAND ENDPOINT
// ------------------------------------------------------------
void AppTransportV1::handleCommand() {
    if (!protocol_) {
        sendJson(503, jsonError("NOT_READY"));
        return;
    }

    const String frame = server_.arg("plain");

    if (frame.length() > MAX_HTTP_BODY_LENGTH) {
        sendJson(413, jsonError("INVALID_PAYLOAD", "FRAME_TOO_LARGE"));
        return;
    }
    if (frame.length() == 0) {
        sendJson(400, jsonError("INVALID_PAYLOAD", "EMPTY_BODY"));
        return;
    }

    // Transport does not parse command names. It simply hands the complete
    // JSON frame to APP_PROTOCOL_V1, keeping the protocol boundary clean.
    const String response = protocol_->processFrame(frame);

    // STARTED means the command has begun and completion will arrive through
    // /api/events. Other results are complete in this HTTP response.
    const int statusCode =
        response.indexOf("\"result\":\"STARTED\"") >= 0 ? 202 :
        (response.indexOf("\"result\":\"ERROR\"") >= 0 ? 400 : 200);
    sendJson(statusCode, response);
}

// ------------------------------------------------------------
// ASYNC RESPONSE POLLING ENDPOINT
// ------------------------------------------------------------
void AppTransportV1::handleEvents() {
    if (queuedResponse_.length() == 0) {
        // 204 is intentionally body-less: there is simply no new event yet.
        server_.send(204);
        return;
    }

    const String response = queuedResponse_;
    queuedResponse_ = "";
    sendJson(200, response);
}

// ------------------------------------------------------------
// HEALTH ENDPOINT
// ------------------------------------------------------------
void AppTransportV1::handleHealth() {
    String body;
    body.reserve(160);
    body += "{\"transport\":\"WIFI_HTTP_V1\",\"protocol\":\"APP_PROTOCOL_V1\",\"status\":\"OK\",\"ip\":\"";
    body += WiFi.softAPIP().toString();
    body += "\"}";
    sendJson(200, body);
}

// ------------------------------------------------------------
// TYPED PACKAGE UPLOAD
// ------------------------------------------------------------
// Android sends one multipart/form-data request to /api/upload?id=<requestId>.
// Each `scene` part contains complete scene text. One `config` part contains
// the complete assignment.config text. This transport never uses the uploaded
// filename as scene identity; the existing storage/upload layers parse and
// validate the internal Scene ID.
void AppTransportV1::handleUploadData() {
    HTTPUpload& upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        // A new HTTP upload request starts here. Query-string `id` is used for
        // response correlation because multipart file parts are data, not the
        // scalar APP_PROTOCOL command envelope.
        if (!uploadRequestStarted_) {
            uploadRequestStarted_ = true;
            uploadRequestId_ = server_.arg("id");
            if (uploadRequestId_.length() == 0 ||
                uploadRequestId_.length() > IndicatorAppProtocol::MAX_REQUEST_ID_LENGTH) {
                uploadFailed_ = true;
                uploadError_ = "MISSING_OR_INVALID_ID";
            }
        }

        if (uploadFailed_) return;

        activeUploadScene_ = -1;
        activeUploadConfig_ = false;

        if (String(upload.name).equalsIgnoreCase("scene")) {
            if (uploadSceneCount_ >= MAX_UPLOAD_SCENES) {
                uploadFailed_ = true;
                uploadError_ = "TOO_MANY_SCENES";
                return;
            }
            activeUploadScene_ = static_cast<int8_t>(uploadSceneCount_++);
            uploadScenes_[activeUploadScene_] = "";
            return;
        }

        if (String(upload.name).equalsIgnoreCase("config")) {
            if (uploadConfigSeen_) {
                uploadFailed_ = true;
                uploadError_ = "DUPLICATE_CONFIG";
                return;
            }
            activeUploadConfig_ = true;
            uploadConfigSeen_ = true;
            uploadConfig_ = "";
            return;
        }

        uploadFailed_ = true;
        uploadError_ = "UNSUPPORTED_UPLOAD_FIELD";
        return;
    }

    if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFailed_) return;

        if (activeUploadScene_ >= 0) {
            if (!appendUploadData(uploadScenes_[activeUploadScene_],
                                  upload.buf,
                                  upload.currentSize)) {
                uploadFailed_ = true;
                uploadError_ = "UPLOAD_BUFFER_FAILED";
            }
            return;
        }

        if (activeUploadConfig_) {
            if (!appendUploadData(uploadConfig_, upload.buf, upload.currentSize)) {
                uploadFailed_ = true;
                uploadError_ = "UPLOAD_BUFFER_FAILED";
            }
            return;
        }

        uploadFailed_ = true;
        uploadError_ = "UPLOAD_DATA_WITHOUT_FIELD";
        return;
    }

    if (upload.status == UPLOAD_FILE_END) {
        activeUploadScene_ = -1;
        activeUploadConfig_ = false;
        return;
    }

    if (upload.status == UPLOAD_FILE_ABORTED) {
        uploadFailed_ = true;
        uploadError_ = "UPLOAD_ABORTED";
        activeUploadScene_ = -1;
        activeUploadConfig_ = false;
    }
}

void AppTransportV1::handleUploadComplete() {
    if (!protocol_) {
        sendJson(503, jsonError("NOT_READY"));
        resetUploadState();
        return;
    }

    if (uploadRequestId_.length() == 0 ||
        uploadRequestId_.length() > IndicatorAppProtocol::MAX_REQUEST_ID_LENGTH) {
        sendJson(400, jsonError("INVALID_PAYLOAD", "MISSING_OR_INVALID_ID"));
        resetUploadState();
        return;
    }

    if (uploadFailed_) {
        sendJson(400, jsonError("INVALID_PAYLOAD", uploadError_.c_str()));
        resetUploadState();
        return;
    }

    if (uploadConfig_.length() == 0) {
        sendJson(400, jsonError("INVALID_PAYLOAD", "MISSING_CONFIG"));
        resetUploadState();
        return;
    }

    IndicatorSceneUpload::UploadSceneText sceneTexts[MAX_UPLOAD_SCENES];
    for (uint8_t i = 0; i < uploadSceneCount_; ++i) {
        sceneTexts[i].text = uploadScenes_[i].c_str();
        sceneTexts[i].length = uploadScenes_[i].length();
    }

    IndicatorAppCommands::UploadPackage package;
    package.scenes = sceneTexts;
    package.sceneCount = uploadSceneCount_;
    package.assignmentConfig = uploadConfig_.c_str();
    package.assignmentConfigLength = uploadConfig_.length();

    const String response = protocol_->uploadPackage(uploadRequestId_, package);
    const int statusCode = response.indexOf("\"result\":\"ERROR\"") >= 0 ? 400 : 200;
    sendJson(statusCode, response);
    resetUploadState();
}

void AppTransportV1::resetUploadState() {
    uploadRequestId_ = "";
    uploadConfig_ = "";
    for (uint8_t i = 0; i < MAX_UPLOAD_SCENES; ++i) uploadScenes_[i] = "";
    uploadConfigSeen_ = false;
    uploadRequestStarted_ = false;
    uploadSceneCount_ = 0;
    activeUploadScene_ = -1;
    activeUploadConfig_ = false;
    uploadFailed_ = false;
    uploadError_ = "";
}

bool AppTransportV1::appendUploadData(String& target,
                                      const uint8_t* data,
                                      size_t length) {
    if (!data && length != 0) return false;
    if (length == 0) return true;
    target.reserve(target.length() + length + 1);
    for (size_t i = 0; i < length; ++i) target += static_cast<char>(data[i]);
    return true;
}

// ------------------------------------------------------------
// FALLBACK / HTTP ERROR
// ------------------------------------------------------------
void AppTransportV1::handleNotFound() {
    sendJson(404, jsonError("NOT_FOUND"));
}

void AppTransportV1::sendJson(int statusCode, const String& body) {
    server_.send(statusCode, "application/json", body);
}

String AppTransportV1::jsonError(const char* error, const char* detail) {
    String out;
    out.reserve(128);
    out += "{\"result\":\"ERROR\",\"error\":\"";
    out += error;
    out += "\"";
    if (detail && detail[0] != '\0') {
        out += ",\"detail\":\"";
        out += detail;
        out += "\"";
    }
    out += "}";
    return out;
}

} // namespace IndicatorAppTransport
