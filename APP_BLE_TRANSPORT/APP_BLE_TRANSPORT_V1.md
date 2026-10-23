# APP BLE TRANSPORT V1

BLE/GATT transport for the transport-independent APP_PROTOCOL_V1.

## Connection hardening

- BLE stack initialization is checked explicitly.
- Local ATT MTU is requested at 517; the peer may negotiate a smaller MTU.
- The server advertises the application service UUID.
- Scan response is enabled for the device name.
- Preferred connection parameters are explicitly set to `0x06` / `0x12`, matching the Arduino-ESP32 BLE server compatibility pattern.
- Advertising is configured to resume after disconnects, with a main-loop fallback.
- BLE callbacks only copy input into fixed buffers; application actions run from the firmware loop.

## GATT characteristics

- `COMMAND_RX` — Android -> ESP command JSON
- `RESPONSE_TX` — ESP -> Android response/event notifications
- `UPLOAD_CONTROL_RX` — Android -> ESP upload control frames
- `UPLOAD_DATA_RX` — Android -> ESP upload data chunks
- `UPLOAD_TX` — ESP -> Android upload acknowledgements/results

## Upload

Upload is a chunked BLE transfer. The existing `UploadPackage` API remains unchanged and is called only after the complete package is received and validated by the transport.

Incomplete uploads are discarded on disconnect or explicit abort.
