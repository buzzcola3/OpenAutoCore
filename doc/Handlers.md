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

## Handler Table

| Handler | Channel | Purpose |
|---|---|---|
| `ControlHandler` | CONTROL | Protocol handshake, version negotiation, ping/keepalive, session lifecycle |
| `InputSourceHandler` | INPUT_SOURCE | Touch and key events to the phone |
| `SensorHandler` | SENSOR | Location, night mode, driving status |
| `BluetoothHandler` | BLUETOOTH | BT pairing and authentication |
| `MediaSourceHandler` | MEDIA_SOURCE_MICROPHONE | Microphone audio capture |
| `PhoneStatusHandler` | PHONE_STATUS | Phone call state |
| `GenericNotificationHandler` | GENERIC_NOTIFICATION | App notifications |
| `NavigationStatusHandler` | NAVIGATION_STATUS | Turn-by-turn nav state |
| `RadioHandler` | RADIO | Radio tuner control |
| `MediaBrowserHandler` | MEDIA_BROWSER | Browse media content |
| `MediaPlaybackStatusHandler` | MEDIA_PLAYBACK_STATUS | Playback state (track, position) |
| `VendorExtensionHandler` | VENDOR_EXTENSION | Vendor-specific messages |
| `MediaSinkVideoHandler` | MEDIA_SINK_VIDEO | H.264/VP9 video frames |
| `MediaSinkAudioHandler` | MEDIA_SINK_MEDIA_AUDIO | Media audio (AAC) |
| `GuidanceAudioHandler` | MEDIA_SINK_GUIDANCE_AUDIO | Navigation voice prompts |
| `SystemAudioHandler` | MEDIA_SINK_SYSTEM_AUDIO | System sounds and alerts |
| `TelephonyAudioHandler` | MEDIA_SINK_TELEPHONY_AUDIO | Phone call voice audio |

## Media Sink Handlers

The 5 media sink handlers (`MediaSinkVideoHandler`, `MediaSinkAudioHandler`, `GuidanceAudioHandler`, `SystemAudioHandler`, `TelephonyAudioHandler`) additionally receive an `OpenAutoTransport` pointer. They decode incoming AA media frames and push raw video/audio data out via `transport->send()` with the appropriate `MsgType`.

## Handlers Fed by OpenAutoTransport

Some handlers expose extra methods that OpenAutoTransport calls when the frontend sends events:

| Handler | Method | Data from frontend |
|---|---|---|
| `InputSourceHandler` | `onTouchEvent()` | Touchscreen coordinates + timestamp |
| `SensorHandler` | `onSensorEvent()` | JSON: location, night mode, driving status |
| `MediaSourceHandler` | `onMicrophoneAudio()` | Microphone PCM audio capture |

These methods encode the data as protobuf and call `send_()` to push it to the phone.

## ControlHandler

`ControlHandler` is special — it manages the entire session lifecycle:
- SSL/TLS handshake (multi-step via `ICryptor`)
- Version negotiation and service discovery
- Ping/keepalive on a dedicated thread (5-second interval, `std::thread` + `std::condition_variable`)
- Session teardown (signals `App` via callback)
