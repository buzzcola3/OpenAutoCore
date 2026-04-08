# ServiceConfig — Frontend-Provided Config

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  Frontend (side B)                                       │
│                                                          │
│  Listens for {"action":"request_config"} from Core       │
│  Responds with full config JSON ({"channels":[...]})     │
└───────────────┬──────────────────────────────────────────┘
                │  shared memory (OpenAutoTransport)
                │  MsgType::CONFIGURATION
                │  payload = JSON string
┌───────────────▼──────────────────────────────────────────┐
│  Core Process (side A)                                   │
│                                                          │
│  Startup:                                                │
│  ├─ start transport                                      │
│  ├─ send {"action":"request_config"} to frontend         │
│  ├─ BLOCK until frontend responds with config JSON       │
│  ├─ setJson() validates + stores                         │
│  └─ proceed with device manager                          │
│                                                          │
│  ServiceConfig (pure JSON→proto converter)               │
│  ├─ setJson(str) → validate + store (or error)           │
│  ├─ getJson()    → current config as JSON string         │
│  ├─ toProto()    → ServiceDiscoveryResponse protobuf     │
│  └─ toTextProto()→ textproto string                      │
└──────────────────────────────────────────────────────────┘
```

## Protocol

Only one interaction:

1. **Core → Frontend:** `{"action":"request_config"}`
2. **Frontend → Core:** Full config JSON (`{"channels":[...]}`)

The config JSON is the same format shown in
`configuration/ServiceDiscoveryResponse.default.json` (kept as a reference).

Core validates the JSON by building a `ServiceDiscoveryResponse` protobuf.
If invalid, the error is logged and Core keeps waiting.
Once a valid config arrives, Core unblocks and starts the device manager.

## Validation

`setJson()` calls `buildProto()` which maps every field into the proto.
Each enum is validated via `_Parse()`. On failure, a descriptive error is returned
and the config is rejected.

## ServiceConfig API

```cpp
class ServiceConfig {
public:
    ServiceConfig() = default;

    std::string getJson() const;
    std::string setJson(const std::string& json);  // returns "" on success, error on failure

    ServiceDiscoveryResponse toProto() const;
    std::string toTextProto() const;
};
```
