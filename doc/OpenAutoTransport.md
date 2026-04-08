# OpenAutoTransport

IPC bridge between OpenAutoCore (the daemon) and the frontend UI process. It's a typed message bus — each message has a `MsgType`, a timestamp, and a raw payload. It has no knowledge of the AA protocol; it just shuttles raw typed blobs between the daemon and whatever renders/captures on the other side.

**External dependency:** `open_auto_transport` library

## Transport Mechanism

Communication between OpenAutoCore and the frontend uses shared memory (Shm). Both sides instantiate an `OpenAutoTransport` — one in the daemon, one in the frontend process.

## Message Types

| MsgType | Direction | Data |
|---|---|---|
| `VIDEO` | Core → Frontend | H.264/VP9 video frames + timestamp |
| `MEDIA_AUDIO` | Core → Frontend | AAC audio codec config + decoded audio |
| `GUIDANCE_AUDIO` | Core → Frontend | Navigation voice prompts |
| `SYSTEM_AUDIO` | Core → Frontend | System sounds and alerts |
| `TELEPHONY_AUDIO` | Core → Frontend | Phone call voice audio |
| `TOUCH` | Frontend → Core | Touchscreen coordinates + timestamp |
| `SENSOR` | Frontend → Core | JSON: location, night mode, driving status |
| `MICROPHONE_AUDIO` | Frontend → Core | Microphone PCM capture |
| `CONFIGURATION` | Bidirectional | Core sends `{"action":"request_config"}` at startup; FE responds with full config JSON |
| `CONTROL` | Frontend → Core | JSON device management commands |

## Sending (Core → Frontend)

Media sink handlers push decoded frames via:

```cpp
transport_->send(buzz::wire::MsgType::VIDEO, timestamp, data, size);
```

## Receiving (Frontend → Core)

Type handlers are registered in `main.cpp` during startup:

```cpp
transport->addTypeHandler(
    buzz::wire::MsgType::TOUCH,
    [app](uint64_t timestamp, const void* data, std::size_t size) {
        if (auto r = app->router())
            r->inputSourceHandler().onTouchEvent(timestamp, data, size);
    });
```

Each handler routes the data to the appropriate handler method which encodes it as protobuf and sends it to the phone.

## Integration

- Created in `main.cpp`, shared between `App` and `FrameRouter`
- Started via `transport->startAsA(std::chrono::microseconds{1000}, false)`
- Only the 5 media sink handlers hold a direct Transport pointer
- Control/input handlers are wired indirectly through `addTypeHandler()` lambdas in `main.cpp`
- `onDeviceListChanged` callback also uses Transport to broadcast the device list JSON to the frontend
