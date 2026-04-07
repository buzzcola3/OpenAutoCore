# Handlers

17 channel handlers, one per Android Auto channel, all owned by FrameRouter. No base class — duck-typed: constructor takes a `SendFn`, `operator()` receives an `InMessage`. Each handler is a bidirectional bridge between the AA protocol and the frontend — the direction depends on the channel's purpose.

**Files:** `include/Lite/*.hpp`, `src/Lite/*.cpp`

## Common Interface

```cpp
struct InMessage {
    messenger::ChannelId channelId;
    messenger::EncryptionType encryptionType;
    messenger::MessageType messageType;
    std::vector<uint8_t> payload;  // includes 2-byte MessageId prefix
};

using SendFn = std::function<void(ChannelId, EncryptionType,
                                   MessageType, const uint8_t*, size_t)>;

class XxxHandler {
public:
    XxxHandler(SendFn sender);
    void operator()(const InMessage& msg);
};
```

Each handler decodes the 2-byte MessageId from the payload, switches on it, and processes the protobuf body. Responses go back to the phone via `send_()`. Some handlers also accept inbound events from OpenAutoTransport (touch, sensors, microphone) which they encode as protobuf and send to the phone.

## Handler Index

| Handler | Channel | Details |
|---|---|---|
| `ControlHandler` | CONTROL | [ControlHandler.md](handlers/ControlHandler.md) |
| `InputSourceHandler` | INPUT_SOURCE | [InputSourceHandler.md](handlers/InputSourceHandler.md) |
| `SensorHandler` | SENSOR | [SensorHandler.md](handlers/SensorHandler.md) |
| `BluetoothHandler` | BLUETOOTH | [BluetoothHandler.md](handlers/BluetoothHandler.md) |
| `MediaSourceHandler` | MEDIA_SOURCE_MICROPHONE | [MediaSourceHandler.md](handlers/MediaSourceHandler.md) |
| Media Sink (5 handlers) | VIDEO, AUDIO, GUIDANCE, SYSTEM, TELEPHONY | [MediaSinkHandlers.md](handlers/MediaSinkHandlers.md) |
| Status / Misc (7 handlers) | PHONE_STATUS, NOTIFICATION, NAV, etc. | [StatusHandlers.md](handlers/StatusHandlers.md) |

## Handlers Fed by OpenAutoTransport

| Handler | Method | Data from frontend |
|---|---|---|
| `InputSourceHandler` | `onTouchEvent()` | Touchscreen coordinates + timestamp |
| `SensorHandler` | `onSensorEvent()` | JSON sensor data |
| `MediaSourceHandler` | `onMicrophoneAudio()` | Microphone PCM audio capture |
