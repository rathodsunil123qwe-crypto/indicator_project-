#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <stdint.h>

#include "../APP_PROTOCOL/app_protocol_v1.h"

namespace IndicatorAppBleTransport {

// ============================================================
// APP BLE TRANSPORT V1
// ============================================================
// BLE/GATT transport for the transport-independent APP_PROTOCOL_V1.
//
// Normal commands:
//   Android -> COMMAND_RX (JSON write)
//   ESP32  -> RESPONSE_TX (JSON notify)
//
// Upload package:
//   Android -> UPLOAD_CONTROL_RX (JSON control frames)
//   Android -> UPLOAD_DATA_RX (raw scene/config bytes)
//   ESP32  -> UPLOAD_TX (JSON status/ack)
//
// The transport does not call Runtime/Motion/SceneVM directly.
// All command execution goes through APP_PROTOCOL_V1.
// ============================================================

static constexpr const char* BLE_DEVICE_NAME = "INDICATOR-V1";

// Custom 128-bit service UUID.
static constexpr const char* BLE_SERVICE_UUID =
    "7D4B0001-4A4D-4E41-9A2D-494E44494341";

// GATT characteristics.
static constexpr const char* BLE_COMMAND_RX_UUID =
    "7D4B0002-4A4D-4E41-9A2D-494E44494341";
static constexpr const char* BLE_RESPONSE_TX_UUID =
    "7D4B0003-4A4D-4E41-9A2D-494E44494341";
static constexpr const char* BLE_UPLOAD_CONTROL_RX_UUID =
    "7D4B0004-4A4D-4E41-9A2D-494E44494341";
static constexpr const char* BLE_UPLOAD_DATA_RX_UUID =
    "7D4B0005-4A4D-4E41-9A2D-494E44494341";
static constexpr const char* BLE_UPLOAD_TX_UUID =
    "7D4B0006-4A4D-4E41-9A2D-494E44494341";

static constexpr size_t MAX_COMMAND_FRAME = IndicatorAppProtocol::MAX_FRAME_LENGTH;
static constexpr uint8_t MAX_UPLOAD_SCENES = 36;

// The existing UploadPackage API is RAM-backed. Keep a transport-side guard so
// a malformed BLE transfer cannot consume the entire ESP32 heap.
static constexpr size_t MAX_BLE_UPLOAD_BYTES = 256 * 1024;
static constexpr size_t MAX_BLE_CHUNK = 512;

// BLE application notification frame:
// [0..1] magic 'I','N'
// [2] channel (1=response, 2=upload)
// [3] flags (bit0=start, bit1=end)
// [4..5] sequence, little-endian
// [6..] payload bytes
static constexpr uint8_t BLE_FRAME_MAGIC_0 = 'I';
static constexpr uint8_t BLE_FRAME_MAGIC_1 = 'N';
static constexpr uint8_t BLE_FRAME_FLAG_START = 0x01;
static constexpr uint8_t BLE_FRAME_FLAG_END = 0x02;

class AppBleTransportV1;

class BleServerCallbacks : public BLEServerCallbacks {
public:
    explicit BleServerCallbacks(AppBleTransportV1& owner) : owner_(owner) {}
    void onConnect(BLEServer* server) override;
    void onDisconnect(BLEServer* server) override;
private:
    AppBleTransportV1& owner_;
};

class BleCommandCallbacks : public BLECharacteristicCallbacks {
public:
    explicit BleCommandCallbacks(AppBleTransportV1& owner) : owner_(owner) {}
    void onWrite(BLECharacteristic* characteristic) override;
private:
    AppBleTransportV1& owner_;
};

class BleUploadControlCallbacks : public BLECharacteristicCallbacks {
public:
    explicit BleUploadControlCallbacks(AppBleTransportV1& owner) : owner_(owner) {}
    void onWrite(BLECharacteristic* characteristic) override;
private:
    AppBleTransportV1& owner_;
};

class BleUploadDataCallbacks : public BLECharacteristicCallbacks {
public:
    explicit BleUploadDataCallbacks(AppBleTransportV1& owner) : owner_(owner) {}
    void onWrite(BLECharacteristic* characteristic) override;
private:
    AppBleTransportV1& owner_;
};

class AppBleTransportV1 {
public:
    bool begin(IndicatorAppProtocol::AppProtocolV1& protocol);
    void update();

    bool running() const { return running_; }
    bool connected() const { return connected_; }

    void onConnect();
    void onDisconnect();

    void receiveCommand(const uint8_t* data, size_t length);
    void receiveUploadControl(const uint8_t* data, size_t length);
    void receiveUploadData(const uint8_t* data, size_t length);

private:
    BLEServer* server_ = nullptr;
    BLEService* service_ = nullptr;
    BLECharacteristic* commandRx_ = nullptr;
    BLECharacteristic* responseTx_ = nullptr;
    BLECharacteristic* uploadControlRx_ = nullptr;
    BLECharacteristic* uploadDataRx_ = nullptr;
    BLECharacteristic* uploadTx_ = nullptr;

    IndicatorAppProtocol::AppProtocolV1* protocol_ = nullptr;
    bool running_ = false;
    volatile bool connected_ = false;

    // Main-loop command handoff. BLE callbacks only copy into fixed buffers;
    // no heap allocation is performed inside BLE callbacks.
    uint8_t pendingCommand_[MAX_COMMAND_FRAME + 1];
    size_t pendingCommandLength_ = 0;
    volatile bool commandPending_ = false;

    // Main-loop upload control handoff.
    uint8_t pendingUploadControl_[MAX_COMMAND_FRAME + 1];
    size_t pendingUploadControlLength_ = 0;
    volatile bool uploadControlPending_ = false;

    // One BLE data chunk is copied by the callback and consumed by loop().
    uint8_t pendingData_[MAX_BLE_CHUNK];
    size_t pendingDataLength_ = 0;
    volatile bool dataPending_ = false;

    // If a client disconnects in the middle of a transfer, abort it cleanly.
    volatile bool disconnectAbortPending_ = false;
    volatile bool restartAdvertisingPending_ = false;
    volatile bool connectionResetPending_ = false;

    // Upload state.
    enum class UploadMode : uint8_t {
        IDLE = 0,
        SCENE,
        CONFIG
    };

    UploadMode uploadMode_ = UploadMode::IDLE;
    bool uploadActive_ = false;
    bool uploadFailed_ = false;
    bool configComplete_ = false;
    uint8_t uploadSceneCount_ = 0;
    uint8_t uploadExpectedSceneCount_ = 0;
    uint8_t currentSceneIndex_ = 0;
    String uploadRequestId_;
    String uploadScenes_[MAX_UPLOAD_SCENES];
    String uploadConfig_;
    size_t uploadTotalBytes_ = 0;
    size_t currentPartBytes_ = 0;
    size_t currentPartExpectedLength_ = 0;

    // Async APP_PROTOCOL completion.
    void pollProtocol();

    // Main-loop handlers.
    void processCommand();
    void processUploadControl();
    void processUploadData();
    void processDisconnectAbort();

    // BLE output.
    void notifyResponse(const String& response);
    void notifyUpload(const String& response);
    void notifyFramed(BLECharacteristic* characteristic, uint8_t channel, const String& payload);

    // Upload state machine.
    bool parseUploadControl(const String& frame,
                            String& op,
                            String& requestId,
                            uint8_t& index,
                            size_t& expectedLength);
    bool extractString(const String& json, const char* key, String& value) const;
    bool extractUnsigned(const String& json, const char* key, size_t& value) const;
    bool appendUploadBytes(String& target, const uint8_t* data, size_t length);
    void resetUploadState();
    void failUpload(const char* error, const char* detail = nullptr);
    void completeUpload();
    void sendUploadAck(const char* op,
                       const char* result,
                       const char* detail = nullptr,
                       int index = -1,
                       size_t bytes = 0);
};

} // namespace IndicatorAppBleTransport
