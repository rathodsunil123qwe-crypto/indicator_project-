#include "app_ble_transport_v1.h"

#include <ctype.h>
#include <string.h>

namespace IndicatorAppBleTransport {
namespace {

static bool jsonHasOp(const String& json, const char* op) {
    String needle = "\"op\":\"";
    needle += op;
    needle += "\"";
    return json.indexOf(needle) >= 0;
}

} // namespace

// ------------------------------------------------------------
// BLE CALLBACKS
// ------------------------------------------------------------

void BleServerCallbacks::onConnect(BLEServer* /*server*/) {
    owner_.onConnect();
}

void BleServerCallbacks::onDisconnect(BLEServer* /*server*/) {
    owner_.onDisconnect();
}

void BleCommandCallbacks::onWrite(BLECharacteristic* characteristic) {
    const String value = characteristic->getValue();
    owner_.receiveCommand(reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
}

void BleUploadControlCallbacks::onWrite(BLECharacteristic* characteristic) {
    const String value = characteristic->getValue();
    owner_.receiveUploadControl(reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
}

void BleUploadDataCallbacks::onWrite(BLECharacteristic* characteristic) {
    const String value = characteristic->getValue();
    owner_.receiveUploadData(reinterpret_cast<const uint8_t*>(value.c_str()), value.length());
}

// ------------------------------------------------------------
// INITIALIZATION
// ------------------------------------------------------------

bool AppBleTransportV1::begin(IndicatorAppProtocol::AppProtocolV1& protocol) {
    protocol_ = &protocol;
    running_ = false;
    connected_ = false;
    pendingCommandLength_ = 0;
    pendingUploadControlLength_ = 0;
    commandPending_ = false;
    uploadControlPending_ = false;
    pendingDataLength_ = 0;
    dataPending_ = false;
    disconnectAbortPending_ = false;
    restartAdvertisingPending_ = false;
    connectionResetPending_ = false;
    resetUploadState();

    if (!BLEDevice::init(BLE_DEVICE_NAME)) {
        Serial.println("[FAIL] App BLE Transport V1 BLE stack initialization");
        return false;
    }

    // Allow the BLE server to resume advertising automatically after a client
    // disconnects. The explicit main-loop restart below is retained as a
    // defensive fallback for stack variants that do not auto-restart.
    BLEDevice::getAdvertising();
    // Prefer the largest practical ATT MTU. Android should request 517 after
    // connecting; the transport still uses peer-MTU framing for notifications.
    if (BLEDevice::setMTU(517) != ESP_OK) {
        Serial.println("[WARN] BLE local MTU 517 request failed; peer may use default MTU");
    }

    server_ = BLEDevice::createServer();
    if (!server_) {
        Serial.println("[FAIL] App BLE Transport V1 server creation");
        return false;
    }
    server_->setCallbacks(new BleServerCallbacks(*this));

    service_ = server_->createService(BLE_SERVICE_UUID);
    if (!service_) {
        Serial.println("[FAIL] App BLE Transport V1 service creation");
        return false;
    }

    commandRx_ = service_->createCharacteristic(
        BLE_COMMAND_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    responseTx_ = service_->createCharacteristic(
        BLE_RESPONSE_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);

    uploadControlRx_ = service_->createCharacteristic(
        BLE_UPLOAD_CONTROL_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    uploadDataRx_ = service_->createCharacteristic(
        BLE_UPLOAD_DATA_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    uploadTx_ = service_->createCharacteristic(
        BLE_UPLOAD_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);

    if (!commandRx_ || !responseTx_ ||
        !uploadControlRx_ || !uploadDataRx_ || !uploadTx_) {
        Serial.println("[FAIL] App BLE Transport V1 characteristic creation");
        return false;
    }

    responseTx_->addDescriptor(new BLE2902());
    uploadTx_->addDescriptor(new BLE2902());

    commandRx_->setCallbacks(new BleCommandCallbacks(*this));
    uploadControlRx_->setCallbacks(new BleUploadControlCallbacks(*this));
    uploadDataRx_->setCallbacks(new BleUploadDataCallbacks(*this));

    service_->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    if (!advertising) {
        Serial.println("[FAIL] App BLE Transport V1 advertising creation");
        return false;
    }

    // Use the same connection-compatibility parameters recommended by the
    // Arduino-ESP32 BLE server examples. These are particularly important for
    // phones that otherwise discover the advertisement but fail during the
    // initial GAP connection negotiation.
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);

    // Make the server explicitly connectable and restart advertising after a
    // disconnect. Do not leave the application dependent on a scanner seeing
    // the name in a scan-response packet.
    server_->advertiseOnDisconnect(true);

    if (!advertising->start()) {
        Serial.println("[FAIL] App BLE Transport V1 advertising start");
        return false;
    }

    running_ = true;

    Serial.printf(
        "[PASS] App BLE Transport V1 started | NAME=%s | SERVICE=%s\n",
        BLE_DEVICE_NAME,
        BLE_SERVICE_UUID);
    Serial.println("[READY] BLE COMMAND_RX | RESPONSE_TX | UPLOAD_CONTROL_RX | UPLOAD_DATA_RX | UPLOAD_TX");

    return true;
}

// ------------------------------------------------------------
// MAIN LOOP
// ------------------------------------------------------------

void AppBleTransportV1::update() {
    if (!running_ || !protocol_) return;

    processDisconnectAbort();
    if (connectionResetPending_) {
        connectionResetPending_ = false;
        resetUploadState();
    }
    if (restartAdvertisingPending_) {
        restartAdvertisingPending_ = false;
        if (server_) {
            if (server_->getConnectedCount() == 0) {
                server_->startAdvertising();
                Serial.println("[BLE] Advertising restarted");
            }
        }
    }
    processCommand();
    processUploadControl();
    processUploadData();
    pollProtocol();
}

// ------------------------------------------------------------
// CONNECTION STATE
// ------------------------------------------------------------

void AppBleTransportV1::onConnect() {
    connected_ = true;
    Serial.println("[BLE] Android/client connected");

    // A new client connection must never inherit a previous incomplete upload.
    // The actual String cleanup is deferred to the main loop.
    connectionResetPending_ = true;
}

void AppBleTransportV1::onDisconnect() {
    connected_ = false;
    disconnectAbortPending_ = true;
    Serial.println("[BLE] Client disconnected");

    // BLE is intentionally available for discovery but there is no persistent
    // application connection. Restart advertising from the main loop so the
    // stack callback remains lightweight.
    restartAdvertisingPending_ = true;
}

void AppBleTransportV1::processDisconnectAbort() {
    if (!disconnectAbortPending_) return;
    disconnectAbortPending_ = false;

    commandPending_ = false;
    pendingCommandLength_ = 0;
    uploadControlPending_ = false;
    pendingUploadControlLength_ = 0;
    dataPending_ = false;
    pendingDataLength_ = 0;

    resetUploadState();
}

// ------------------------------------------------------------
// INPUT HANDOFF
// ------------------------------------------------------------

void AppBleTransportV1::receiveCommand(const uint8_t* data, size_t length) {
    if (!connected_) return;

    if (!data || length == 0 || length > MAX_COMMAND_FRAME) {
        const char* error = "{\"id\":\"\",\"result\":\"ERROR\",\"error\":\"INVALID_PAYLOAD\",\"detail\":\"FRAME_LENGTH\"}";
        const size_t errorLength = strlen(error);
        memcpy(pendingCommand_, error, errorLength);
        pendingCommand_[errorLength] = '\0';
        pendingCommandLength_ = errorLength;
        commandPending_ = true;
        return;
    }

    // Android must wait for the response before sending the next command.
    if (commandPending_) {
        const char* error = "{\"id\":\"\",\"result\":\"ERROR\",\"error\":\"BUSY\",\"detail\":\"COMMAND_QUEUE_FULL\"}";
        const size_t errorLength = strlen(error);
        memcpy(pendingCommand_, error, errorLength);
        pendingCommand_[errorLength] = '\0';
        pendingCommandLength_ = errorLength;
        commandPending_ = true;
        return;
    }

    memcpy(pendingCommand_, data, length);
    pendingCommand_[length] = '\0';
    pendingCommandLength_ = length;
    commandPending_ = true;
}

void AppBleTransportV1::receiveUploadControl(const uint8_t* data, size_t length) {
    if (!connected_) return;

    if (!data || length == 0 || length > MAX_COMMAND_FRAME) {
        const char* error = "{\"op\":\"ERROR\",\"result\":\"ERROR\",\"detail\":\"FRAME_LENGTH\"}";
        const size_t errorLength = strlen(error);
        memcpy(pendingUploadControl_, error, errorLength);
        pendingUploadControl_[errorLength] = '\0';
        pendingUploadControlLength_ = errorLength;
        uploadControlPending_ = true;
        return;
    }

    if (uploadControlPending_) {
        const char* error = "{\"op\":\"ERROR\",\"result\":\"ERROR\",\"detail\":\"CONTROL_QUEUE_FULL\"}";
        const size_t errorLength = strlen(error);
        memcpy(pendingUploadControl_, error, errorLength);
        pendingUploadControl_[errorLength] = '\0';
        pendingUploadControlLength_ = errorLength;
        uploadControlPending_ = true;
        return;
    }

    memcpy(pendingUploadControl_, data, length);
    pendingUploadControl_[length] = '\0';
    pendingUploadControlLength_ = length;
    uploadControlPending_ = true;
}

void AppBleTransportV1::receiveUploadData(const uint8_t* data, size_t length) {
    if (!connected_) return;

    if (!uploadActive_ || uploadMode_ == UploadMode::IDLE) {
        // Do not silently accept data outside an active part.
        notifyUpload("{\"op\":\"DATA\",\"result\":\"ERROR\",\"detail\":\"NO_ACTIVE_UPLOAD_PART\"}");
        return;
    }

    if (!data || length == 0 || length > MAX_BLE_CHUNK) {
        notifyUpload("{\"op\":\"DATA\",\"result\":\"ERROR\",\"detail\":\"INVALID_CHUNK_SIZE\"}");
        return;
    }

    // A single pending chunk is intentional: Android gets an ACK after the
    // main loop consumes it, preventing an unbounded callback-side queue.
    if (dataPending_) {
        notifyUpload("{\"op\":\"DATA\",\"result\":\"ERROR\",\"detail\":\"CHUNK_QUEUE_FULL\"}");
        return;
    }

    memcpy(pendingData_, data, length);
    pendingDataLength_ = length;
    dataPending_ = true;
}

// ------------------------------------------------------------
// COMMAND PATH
// ------------------------------------------------------------

void AppBleTransportV1::processCommand() {
    if (!commandPending_) return;

    String frame;
    frame.reserve(pendingCommandLength_ + 1);
    for (size_t i = 0; i < pendingCommandLength_; ++i) frame += static_cast<char>(pendingCommand_[i]);
    pendingCommandLength_ = 0;
    commandPending_ = false;

    const String response = protocol_->processFrame(frame);
    notifyResponse(response);
    Serial.printf("[BLE COMMAND RESPONSE] %s\n", response.c_str());
}

// ------------------------------------------------------------
// ASYNC APP PROTOCOL PATH
// ------------------------------------------------------------

void AppBleTransportV1::pollProtocol() {
    String response;
    if (!protocol_->update(response)) return;

    notifyResponse(response);
    Serial.printf("[BLE EVENT] %s\n", response.c_str());
}

// ------------------------------------------------------------
// UPLOAD CONTROL
// ------------------------------------------------------------

void AppBleTransportV1::processUploadControl() {
    if (!uploadControlPending_) return;

    String frame;
    frame.reserve(pendingUploadControlLength_ + 1);
    for (size_t i = 0; i < pendingUploadControlLength_; ++i) frame += static_cast<char>(pendingUploadControl_[i]);
    pendingUploadControlLength_ = 0;
    uploadControlPending_ = false;

    String op;
    String requestId;
    uint8_t index = 0;
    size_t expectedLength = 0;

    if (!parseUploadControl(frame, op, requestId, index, expectedLength)) {
        notifyUpload("{\"op\":\"ERROR\",\"result\":\"ERROR\",\"detail\":\"INVALID_CONTROL\"}");
        return;
    }

    if (op.equalsIgnoreCase("BEGIN")) {
        if (uploadActive_) {
            notifyUpload("{\"op\":\"BEGIN\",\"result\":\"ERROR\",\"detail\":\"UPLOAD_ALREADY_ACTIVE\"}");
            return;
        }

        size_t sceneCount = 0;
        if (!extractUnsigned(frame, "sceneCount", sceneCount) ||
            sceneCount == 0 || sceneCount > MAX_UPLOAD_SCENES) {
            notifyUpload("{\"op\":\"BEGIN\",\"result\":\"ERROR\",\"detail\":\"INVALID_SCENE_COUNT\"}");
            return;
        }

        if (requestId.length() == 0 ||
            requestId.length() > IndicatorAppProtocol::MAX_REQUEST_ID_LENGTH) {
            notifyUpload("{\"op\":\"BEGIN\",\"result\":\"ERROR\",\"detail\":\"MISSING_OR_INVALID_ID\"}");
            return;
        }

        resetUploadState();
        uploadActive_ = true;
        uploadRequestId_ = requestId;
        // expectedLength is intentionally ignored at BEGIN; each part declares
        // its own expected byte length.
        uploadSceneCount_ = 0;
        uploadExpectedSceneCount_ = static_cast<uint8_t>(sceneCount);
        currentSceneIndex_ = 0;

        notifyUpload("{\"op\":\"BEGIN\",\"result\":\"READY\"}");
        return;
    }

    if (!uploadActive_) {
        notifyUpload("{\"op\":\"" + op + "\",\"result\":\"ERROR\",\"detail\":\"NO_ACTIVE_UPLOAD\"}");
        return;
    }

    if (op.equalsIgnoreCase("ABORT")) {
        resetUploadState();
        notifyUpload("{\"op\":\"ABORT\",\"result\":\"OK\"}");
        return;
    }

    if (uploadFailed_) {
        notifyUpload("{\"op\":\"UPLOAD\",\"result\":\"ERROR\",\"detail\":\"UPLOAD_FAILED_ABORT_REQUIRED\"}");
        return;
    }

    if (op.equalsIgnoreCase("SCENE_BEGIN")) {
        if (uploadMode_ != UploadMode::IDLE) {
            notifyUpload("{\"op\":\"SCENE_BEGIN\",\"result\":\"ERROR\",\"detail\":\"PART_ALREADY_ACTIVE\"}");
            return;
        }
        if (index != uploadSceneCount_) {
            notifyUpload("{\"op\":\"SCENE_BEGIN\",\"result\":\"ERROR\",\"detail\":\"SCENE_INDEX_OUT_OF_ORDER\"}");
            return;
        }
        if (expectedLength == 0 || expectedLength > MAX_BLE_UPLOAD_BYTES ||
            uploadTotalBytes_ + expectedLength > MAX_BLE_UPLOAD_BYTES) {
            notifyUpload("{\"op\":\"SCENE_BEGIN\",\"result\":\"ERROR\",\"detail\":\"UPLOAD_TOO_LARGE\"}");
            return;
        }

        uploadMode_ = UploadMode::SCENE;
        currentSceneIndex_ = index;
        uploadScenes_[index] = "";
        if (!uploadScenes_[index].reserve(expectedLength + 1)) {
            failUpload("BUFFER_ALLOC_FAILED", "SCENE");
            return;
        }
        currentPartBytes_ = 0;
        // Expected length is tracked in currentPartExpectedLength_.
        currentPartExpectedLength_ = expectedLength;
        sendUploadAck("SCENE_BEGIN", "READY", nullptr, index, 0);
        return;
    }

    if (op.equalsIgnoreCase("SCENE_END")) {
        if (uploadMode_ != UploadMode::SCENE) {
            notifyUpload("{\"op\":\"SCENE_END\",\"result\":\"ERROR\",\"detail\":\"NO_ACTIVE_SCENE\"}");
            return;
        }
        if (currentPartBytes_ != currentPartExpectedLength_) {
            notifyUpload("{\"op\":\"SCENE_END\",\"result\":\"ERROR\",\"detail\":\"LENGTH_MISMATCH\"}");
            return;
        }
        uploadTotalBytes_ += currentPartBytes_;
        uploadSceneCount_++;
        uploadMode_ = UploadMode::IDLE;
        currentPartBytes_ = 0;
        currentPartExpectedLength_ = 0;
        sendUploadAck("SCENE_END", "OK", nullptr, currentSceneIndex_, uploadScenes_[currentSceneIndex_].length());
        return;
    }

    if (op.equalsIgnoreCase("CONFIG_BEGIN")) {
        if (uploadMode_ != UploadMode::IDLE) {
            notifyUpload("{\"op\":\"CONFIG_BEGIN\",\"result\":\"ERROR\",\"detail\":\"PART_ALREADY_ACTIVE\"}");
            return;
        }
        if (expectedLength == 0 || expectedLength > MAX_BLE_UPLOAD_BYTES ||
            uploadTotalBytes_ + expectedLength > MAX_BLE_UPLOAD_BYTES) {
            notifyUpload("{\"op\":\"CONFIG_BEGIN\",\"result\":\"ERROR\",\"detail\":\"UPLOAD_TOO_LARGE\"}");
            return;
        }

        uploadMode_ = UploadMode::CONFIG;
        uploadConfig_ = "";
        if (!uploadConfig_.reserve(expectedLength + 1)) {
            failUpload("BUFFER_ALLOC_FAILED", "CONFIG");
            return;
        }
        currentPartBytes_ = 0;
        currentPartExpectedLength_ = expectedLength;
        sendUploadAck("CONFIG_BEGIN", "READY", nullptr, -1, 0);
        return;
    }

    if (op.equalsIgnoreCase("CONFIG_END")) {
        if (uploadMode_ != UploadMode::CONFIG) {
            notifyUpload("{\"op\":\"CONFIG_END\",\"result\":\"ERROR\",\"detail\":\"NO_ACTIVE_CONFIG\"}");
            return;
        }
        if (currentPartBytes_ != currentPartExpectedLength_) {
            notifyUpload("{\"op\":\"CONFIG_END\",\"result\":\"ERROR\",\"detail\":\"LENGTH_MISMATCH\"}");
            return;
        }
        uploadTotalBytes_ += currentPartBytes_;
        configComplete_ = true;
        uploadMode_ = UploadMode::IDLE;
        currentPartBytes_ = 0;
        currentPartExpectedLength_ = 0;
        sendUploadAck("CONFIG_END", "OK", nullptr, -1, uploadConfig_.length());
        return;
    }

    if (op.equalsIgnoreCase("COMMIT")) {
        completeUpload();
        return;
    }

    notifyUpload("{\"op\":\"ERROR\",\"result\":\"ERROR\",\"detail\":\"UNKNOWN_UPLOAD_OPERATION\"}");
}

void AppBleTransportV1::processUploadData() {
    if (!dataPending_) return;

    if (!uploadActive_ || uploadMode_ == UploadMode::IDLE) {
        dataPending_ = false;
        pendingDataLength_ = 0;
        return;
    }

    const size_t length = pendingDataLength_;
    const uint8_t* data = pendingData_;

    String* target = nullptr;
    if (uploadMode_ == UploadMode::SCENE) {
        target = &uploadScenes_[currentSceneIndex_];
    } else if (uploadMode_ == UploadMode::CONFIG) {
        target = &uploadConfig_;
    }

    if (!target ||
        currentPartBytes_ + length > currentPartExpectedLength_ ||
        uploadTotalBytes_ + currentPartBytes_ + length > MAX_BLE_UPLOAD_BYTES) {
        dataPending_ = false;
        pendingDataLength_ = 0;
        failUpload("UPLOAD_BUFFER_FAILED", "LENGTH_OR_SIZE_LIMIT");
        return;
    }

    if (!appendUploadBytes(*target, data, length)) {
        dataPending_ = false;
        pendingDataLength_ = 0;
        failUpload("UPLOAD_BUFFER_FAILED");
        return;
    }

    currentPartBytes_ += length;

    dataPending_ = false;
    pendingDataLength_ = 0;

    sendUploadAck("DATA", "OK", nullptr,
                  uploadMode_ == UploadMode::SCENE ? currentSceneIndex_ : -1,
                  currentPartBytes_);
}

// ------------------------------------------------------------
// UPLOAD COMMIT
// ------------------------------------------------------------

void AppBleTransportV1::completeUpload() {
    if (!uploadActive_) {
        notifyUpload("{\"op\":\"COMMIT\",\"result\":\"ERROR\",\"detail\":\"NO_ACTIVE_UPLOAD\"}");
        return;
    }
    if (uploadMode_ != UploadMode::IDLE) {
        notifyUpload("{\"op\":\"COMMIT\",\"result\":\"ERROR\",\"detail\":\"PART_STILL_ACTIVE\"}");
        return;
    }
    if (uploadSceneCount_ == 0 || uploadSceneCount_ != uploadExpectedSceneCount_ || !configComplete_) {
        notifyUpload("{\"op\":\"COMMIT\",\"result\":\"ERROR\",\"detail\":\"INCOMPLETE_PACKAGE\"}");
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
    notifyResponse(response);
    Serial.printf("[BLE UPLOAD RESPONSE] %s\n", response.c_str());

    resetUploadState();
}

// ------------------------------------------------------------
// UPLOAD PARSING / HELPERS
// ------------------------------------------------------------

bool AppBleTransportV1::parseUploadControl(const String& frame,
                                           String& op,
                                           String& requestId,
                                           uint8_t& index,
                                           size_t& expectedLength) {
    op = "";
    requestId = "";
    index = 0;
    expectedLength = 0;

    if (frame.length() == 0 || frame.length() > MAX_COMMAND_FRAME ||
        frame[0] != '{' || frame[frame.length() - 1] != '}') {
        return false;
    }

    if (!extractString(frame, "op", op)) return false;
    extractString(frame, "id", requestId);

    size_t number = 0;
    if (extractUnsigned(frame, "index", number)) {
        if (number > 255) return false;
        index = static_cast<uint8_t>(number);
    }

    if (extractUnsigned(frame, "length", number)) {
        expectedLength = number;
    }

    return true;
}

bool AppBleTransportV1::extractString(const String& json,
                                      const char* key,
                                      String& value) const {
    String needle = "\"";
    needle += key;
    needle += "\"";
    const int keyPos = json.indexOf(needle);
    if (keyPos < 0) return false;

    int colon = json.indexOf(':', keyPos + needle.length());
    if (colon < 0) return false;

    int quote = colon + 1;
    while (quote < static_cast<int>(json.length()) &&
           (json[quote] == ' ' || json[quote] == '\t' ||
            json[quote] == '\r' || json[quote] == '\n')) {
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

bool AppBleTransportV1::extractUnsigned(const String& json,
                                        const char* key,
                                        size_t& value) const {
    String needle = "\"";
    needle += key;
    needle += "\"";
    const int keyPos = json.indexOf(needle);
    if (keyPos < 0) return false;

    int colon = json.indexOf(':', keyPos + needle.length());
    if (colon < 0) return false;

    int pos = colon + 1;
    while (pos < static_cast<int>(json.length()) &&
           (json[pos] == ' ' || json[pos] == '\t' ||
            json[pos] == '\r' || json[pos] == '\n')) {
        ++pos;
    }

    if (pos >= static_cast<int>(json.length()) ||
        !isdigit(static_cast<unsigned char>(json[pos]))) {
        return false;
    }

    size_t number = 0;
    while (pos < static_cast<int>(json.length()) &&
           isdigit(static_cast<unsigned char>(json[pos]))) {
        const size_t digit = static_cast<size_t>(json[pos] - '0');
        if (number > (SIZE_MAX - digit) / 10) return false;
        number = number * 10 + digit;
        ++pos;
    }

    value = number;
    return true;
}

bool AppBleTransportV1::appendUploadBytes(String& target,
                                          const uint8_t* data,
                                          size_t length) {
    if (!data || length == 0) return false;

    if (target.length() + length > MAX_BLE_UPLOAD_BYTES) return false;

    // Leave headroom for the runtime, BLE stack, and String allocator.
    const size_t heap = ESP.getFreeHeap();
    if (heap < length + 16384) return false;

    if (!target.reserve(target.length() + length + 1)) return false;

    for (size_t i = 0; i < length; ++i) {
        target += static_cast<char>(data[i]);
    }
    return true;
}

void AppBleTransportV1::failUpload(const char* error, const char* detail) {
    uploadFailed_ = true;
    uploadMode_ = UploadMode::IDLE;

    String out = "{\"op\":\"UPLOAD\",\"result\":\"ERROR\",\"error\":\"";
    out += error;
    out += "\"";
    if (detail && detail[0] != '\0') {
        out += ",\"detail\":\"";
        out += detail;
        out += "\"";
    }
    out += "}";
    notifyUpload(out);
}

void AppBleTransportV1::resetUploadState() {
    uploadActive_ = false;
    uploadFailed_ = false;
    configComplete_ = false;
    uploadSceneCount_ = 0;
    uploadExpectedSceneCount_ = 0;
    currentSceneIndex_ = 0;
    uploadRequestId_ = "";
    uploadConfig_ = "";
    uploadTotalBytes_ = 0;
    currentPartBytes_ = 0;
    currentPartExpectedLength_ = 0;
    uploadMode_ = UploadMode::IDLE;

    for (uint8_t i = 0; i < MAX_UPLOAD_SCENES; ++i) {
        uploadScenes_[i] = "";
    }
}

void AppBleTransportV1::sendUploadAck(const char* op,
                                      const char* result,
                                      const char* detail,
                                      int index,
                                      size_t bytes) {
    String out;
    out.reserve(160);
    out += "{\"op\":\"";
    out += op;
    out += "\",\"result\":\"";
    out += result;
    out += "\"";

    if (index >= 0) {
        out += ",\"index\":";
        out += index;
    }

    if (bytes > 0) {
        out += ",\"bytes\":";
        out += static_cast<unsigned long>(bytes);
    }

    if (detail && detail[0] != '\0') {
        out += ",\"detail\":\"";
        out += detail;
        out += "\"";
    }

    out += "}";
    notifyUpload(out);
}

void AppBleTransportV1::notifyResponse(const String& response) {
    if (!connected_ || !responseTx_) return;
    notifyFramed(responseTx_, 1, response);
}

void AppBleTransportV1::notifyUpload(const String& response) {
    if (!connected_ || !uploadTx_) return;
    notifyFramed(uploadTx_, 2, response);
}

void AppBleTransportV1::notifyFramed(BLECharacteristic* characteristic,
                                     uint8_t channel,
                                     const String& payload) {
    if (!connected_ || !characteristic) return;

    size_t mtu = 23;
    if (server_) {
        const auto peers = server_->getPeerDevices(false);
        if (!peers.empty()) {
            mtu = server_->getPeerMTU(peers.begin()->first);
        }
    }

    // ATT notification payload = MTU - 3. Reserve six bytes for our
    // transport frame header. With the default MTU 23 this gives 14 bytes.
    size_t maxPayload = (mtu > 9) ? (mtu - 3 - 6) : 14;
    if (maxPayload > 240) maxPayload = 240;
    if (maxPayload == 0) maxPayload = 14;

    uint16_t sequence = 0;
    size_t offset = 0;

    // Empty payload still produces one complete frame.
    do {
        const size_t remaining = payload.length() - offset;
        const size_t count = remaining > maxPayload ? maxPayload : remaining;

        uint8_t frame[246];
        frame[0] = BLE_FRAME_MAGIC_0;
        frame[1] = BLE_FRAME_MAGIC_1;
        frame[2] = channel;
        frame[3] = 0;
        if (offset == 0) frame[3] |= BLE_FRAME_FLAG_START;
        if (offset + count >= payload.length()) frame[3] |= BLE_FRAME_FLAG_END;
        frame[4] = static_cast<uint8_t>(sequence & 0xFF);
        frame[5] = static_cast<uint8_t>((sequence >> 8) & 0xFF);

        if (count > 0) {
            memcpy(&frame[6], payload.c_str() + offset, count);
        }

       
        characteristic->setValue(frame, count + 6);
        characteristic->notify();

        offset += count;
        ++sequence;

        // Allow the BLE stack time between notification frames.
        if (offset < payload.length()) {
            delay(10);
        }
    } while (offset < payload.length());

}

} // namespace IndicatorAppBleTransport
