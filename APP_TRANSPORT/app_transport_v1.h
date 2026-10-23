#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include <stdint.h>

#include "../APP_PROTOCOL/app_protocol_v1.h"

namespace IndicatorAppTransport {

// ============================================================
// APP TRANSPORT V1
// ============================================================
// Wi-Fi transport only. This layer owns the network connection and HTTP
// framing; APP_PROTOCOL_V1 remains responsible for the JSON contract.
//
// V1 uses ESP32 SoftAP mode so the Android app can connect directly to the
// controller without a home/router network. A later STA/router mode can be
// added without changing APP_PROTOCOL_V1 or APP_COMMANDS_V1.
// ============================================================

static constexpr const char* WIFI_AP_SSID = "INDICATOR-V1";
static constexpr const char* WIFI_AP_PASSWORD = "indicator1";
static constexpr uint16_t HTTP_PORT = 80;
static constexpr size_t MAX_HTTP_BODY_LENGTH = 512;
static constexpr uint8_t MAX_UPLOAD_SCENES = 36;

class AppTransportV1 {
public:
    bool begin(IndicatorAppProtocol::AppProtocolV1& protocol);
    void update();

    bool running() const { return running_; }

private:
    WebServer server_{HTTP_PORT};
    IndicatorAppProtocol::AppProtocolV1* protocol_ = nullptr;
    bool running_ = false;

    // One async APP_COMMANDS operation can be active at a time. Keep its
    // completion response until Android retrieves it from /api/events.
    String queuedResponse_;

    void handleCommand();
    void handleEvents();
    void handleHealth();
    void handleUploadComplete();
    void handleUploadData();
    void handleNotFound();

    void resetUploadState();
    bool appendUploadData(String& target, const uint8_t* data, size_t length);

    void sendJson(int statusCode, const String& body);
    static String jsonError(const char* error, const char* detail = nullptr);

    // Dedicated multipart upload state. The buffers are only live for one
    // /api/upload request and are passed into the existing typed UploadPackage
    // API after the complete request has arrived.
    String uploadRequestId_;
    String uploadConfig_;
    String uploadScenes_[MAX_UPLOAD_SCENES];
    uint8_t uploadSceneCount_ = 0;
    int8_t activeUploadScene_ = -1;
    bool activeUploadConfig_ = false;
    bool uploadConfigSeen_ = false;
    bool uploadRequestStarted_ = false;
    bool uploadFailed_ = false;
    String uploadError_;
};

} // namespace IndicatorAppTransport
