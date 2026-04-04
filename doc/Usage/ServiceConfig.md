# ServiceConfig — Frontend Usage

The frontend (side B) communicates with the core (side A) over shared-memory transport using `MsgType::CONFIGURATION`. Payloads are raw JSON strings.

## Sending a request

```cpp
#include <open_auto_transport/transport.hpp>
#include <nlohmann/json.hpp>

nlohmann::json req;
req["action"] = "set";          // "get", "set", or "reset"
req["config"] = { /* ... */ };  // required for "set" only

auto bytes = req.dump();
transport->send(buzz::wire::MsgType::CONFIGURATION, 0,
                bytes.data(), bytes.size());
```

## Actions

### `get`

The core responds with the current config JSON sent back over `MsgType::CONFIGURATION`. If a user config exists it returns that; otherwise it returns the shipped default.

```json
{ "action": "get" }
```

The response is the raw JSON config object (same schema as the `config` field in `set`).

### `set`

Replaces the running config. The core validates the JSON by constructing the protobuf. If validation succeeds the config is saved to `configuration/ServiceDiscoveryResponse.user.json`. If it fails the previous config is kept and the request is silently dropped.

```json
{
  "action": "set",
  "config": {
    "channels": [ ... ],
    "driver_position": "DRIVER_POSITION_LEFT",
    "display_name": "MyHead",
    ...
  }
}
```

The `config` object has the same schema as `configuration/ServiceDiscoveryResponse.default.json`. See the full default below for all fields.

### `reset`

Deletes the user config and reloads from the shipped default. Fire-and-forget.

```json
{ "action": "reset" }
```

## Config schema

The `config` object passed to `set` must match the structure of the default config. Key top-level fields:

| Field | Type | Example |
|-------|------|---------|
| `channels` | array | See channel types below |
| `driver_position` | string enum | `"DRIVER_POSITION_LEFT"`, `"DRIVER_POSITION_RIGHT"` |
| `can_play_native_media_during_vr` | bool | `false` |
| `display_name` | string | `"OpenAutoCore"` |
| `probe_for_support` | bool | `false` |
| `connection_configuration` | object | Ping settings |
| `headunit_info` | object | Make, model, year, etc. |

### Channel types

Each channel has an `id` and exactly one service key:

| Service key | Channel id | Purpose |
|---|---|---|
| `media_sink_service` (video) | 3 | H.264 video sink |
| `media_source_service` | 9 | Microphone PCM source |
| `sensor_source_service` | 1 | Driving status, location, night mode |
| `input_source_service` | 8 | Touchscreen input |
| `bluetooth_service` | 10 | Bluetooth pairing |
| `media_sink_service` (media audio) | 4 | Media PCM audio sink |
| `media_sink_service` (system audio) | 6 | System PCM audio sink |
| `media_sink_service` (guidance audio) | 5 | Guidance PCM audio sink |

Audio sink channels are distinguished by `audio_type`: `AUDIO_STREAM_MEDIA`, `AUDIO_STREAM_SYSTEM_AUDIO`, or `AUDIO_STREAM_GUIDANCE`.

### Enum values

**`available_type`** (media): `MEDIA_CODEC_VIDEO_H264_BP`, `MEDIA_CODEC_AUDIO_PCM`

**`audio_type`**: `AUDIO_STREAM_MEDIA`, `AUDIO_STREAM_SYSTEM_AUDIO`, `AUDIO_STREAM_GUIDANCE`

**`codec_resolution`**: `VIDEO_800x480`, `VIDEO_1280x720`, `VIDEO_1920x1080`

**`frame_rate`**: `VIDEO_FPS_30`, `VIDEO_FPS_60`

**`sensor_type`**: `SENSOR_DRIVING_STATUS_DATA`, `SENSOR_LOCATION`, `SENSOR_NIGHT_MODE`

**`supported_pairing_methods`**: `BLUETOOTH_PAIRING_UNAVAILABLE`, `BLUETOOTH_PAIRING_HFP`

**`driver_position`**: `DRIVER_POSITION_LEFT`, `DRIVER_POSITION_RIGHT`

### Validation

The core validates by constructing the full `ServiceDiscoveryResponse` protobuf from the JSON. Any unrecognised enum string or structural mismatch causes the `set` to be silently rejected. The previous config remains active.

## File layout

| File | Purpose |
|------|---------|
| `configuration/ServiceDiscoveryResponse.default.json` | Shipped default (read-only, checked in) |
| `configuration/ServiceDiscoveryResponse.user.json` | User overlay (written by `set`, deleted by `reset`) |
