# ServiceConfig — Simplified JSON-Based Config (Implemented)

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  UI Process (side B)                                     │
│                                                          │
│  Sends JSON RPC over MsgType::configuration              │
│  Actions: "get", "set", "reset"                          │
└───────────────┬──────────────────────────────────────────┘
                │  shared memory (OpenAutoTransport)
                │  MsgType::configuration (requires v0.0.26)
                │  payload = JSON string
┌───────────────▼──────────────────────────────────────────┐
│  Core Process (side A)                                   │
│                                                          │
│  addTypeHandler(MsgType::configuration, handler)         │
│  ├─ parse JSON request                                   │
│  ├─ dispatch to ServiceConfig (get/set/reset)            │
│  ├─ build JSON response                                  │
│  └─ transport->send(MsgType::configuration, response)    │
│                                                          │
│  ServiceConfig                                           │
│  ├─ load()       → defaults.json + user.json             │
│  ├─ getJson()    → current config as JSON string         │
│  ├─ setJson(str) → validate + replace (or error)         │
│  ├─ resetJson()  → delete user file, reload defaults     │
│  ├─ save()       → write user JSON to disk               │
│  ├─ toProto()    → ServiceDiscoveryResponse protobuf     │
│  └─ toTextProto()→ textproto string                      │
└──────────────────────────────────────────────────────────┘
```

### Config File Flow

```
ServiceDiscoveryResponse.default.json  ← shipped defaults (read-only, checked in)
  │ load as base
  ▼
UserServiceDiscoveryResponse.json      ← runtime override (user-editable, gitignored)
  │ full replacement if present
  ▼
    ServiceConfig                      ← C++ class holding JSON config
  │ toProto()
  ▼
ServiceDiscoveryResponse (protobuf)    ← used for AndroidAuto service discovery
```

### Validation

`setJson()` validates by attempting to construct a `ServiceDiscoveryResponse` protobuf
from the candidate JSON. Each enum field is validated using the proto's `_Parse()`
functions. If any value is invalid, the previous config is kept and a descriptive
error message is returned (e.g. `"invalid driver_position value: 'BAD_VALUE'"`).

---

## Files

| File | Status | Purpose |
|------|--------|---------|
| `configuration/ServiceDiscoveryResponse.default.json` | **created** | Checked-in default config |
| `include/Configuration/ServiceConfig.hpp` | **created** | ServiceConfig class header |
| `src/Configuration/ServiceConfig.cpp` | **created** | Implementation with JSON→proto mapper |
| `include/Service/AndroidAutoEntity.hpp` | **modified** | Added `ServiceConfig&` member |
| `include/Service/AndroidAutoEntityFactory.hpp` | **modified** | Threads `ServiceConfig&` through |
| `src/Service/AndroidAutoEntityFactory.cpp` | **modified** | Passes `ServiceConfig&` to entity |
| `src/Service/AndroidAutoEntity.cpp` | **modified** | Uses `serviceConfig_.toProto()` in service discovery |
| `src/autoapp.cpp` | **modified** | Creates ServiceConfig, passes to factory |
| `BUILD.bazel` | **modified** | Added `@nlohmann_json//:json` dep |

### Pending (requires OpenAutoTransport v0.0.26)

| File | Action | Purpose |
|------|--------|---------|
| `MODULE.bazel` | **modify** | Bump `open_auto_transport` to `v0.0.26` |
| `src/autoapp.cpp` | **modify** | Register `MsgType::configuration` handler |

---

## RPC Protocol (JSON over MsgType::configuration)

No protobuf is needed for the RPC envelope — both request and response are JSON strings
sent over the shared-memory transport.

### Request

```json
{"id": 1, "action": "get"}
{"id": 2, "action": "set", "config": { ... full config JSON ... }}
{"id": 3, "action": "reset"}
```

### Response

```json
{"id": 1, "ok": true, "config": { ... current config JSON ... }}
{"id": 2, "ok": true}
{"id": 2, "ok": false, "error": "invalid driver_position value: 'BAD'"}
{"id": 3, "ok": true, "config": { ... default config JSON ... }}
```

### Handler (to be registered when MsgType::configuration is available)

```cpp
transport->addTypeHandler(
    buzz::wire::MsgType::CONFIGURATION,
    [&serviceConfig, transport](uint64_t, const void* data, std::size_t size) {
        auto req = nlohmann::json::parse(
            static_cast<const char*>(data),
            static_cast<const char*>(data) + size,
            nullptr, false);
        if (req.is_discarded()) return;

        nlohmann::json resp;
        resp["id"] = req.value("id", 0);

        auto action = req.value("action", "");
        if (action == "get") {
            resp["ok"] = true;
            resp["config"] = nlohmann::json::parse(serviceConfig.getJson());
        } else if (action == "set") {
            auto err = serviceConfig.setJson(req["config"].dump());
            resp["ok"] = err.empty();
            if (!err.empty()) resp["error"] = err;
            else serviceConfig.save();
        } else if (action == "reset") {
            resp["ok"] = serviceConfig.reset();
            resp["config"] = nlohmann::json::parse(serviceConfig.getJson());
        } else {
            resp["ok"] = false;
            resp["error"] = "unknown action: " + action;
        }

        auto bytes = resp.dump();
        transport->send(buzz::wire::MsgType::CONFIGURATION, 0,
                        bytes.data(), bytes.size());
    });
```

---

## ServiceConfig API

```cpp
class ServiceConfig {
public:
    ServiceConfig(std::string defaultPath, std::string userPath);

    bool load();                          // Load default + user JSON
    bool save();                          // Save to user JSON file
    bool reset();                         // Delete user file, reload defaults

    std::string getJson() const;          // Current config as JSON string
    std::string setJson(const std::string& json);  // Validate + replace (returns error or "")

    ServiceDiscoveryResponse toProto() const;       // Build protobuf
    std::string toTextProto() const;                // Build textproto string
};
```

### Validation by Proto Construction

`setJson()` and `load()` call an internal `buildProto()` mapper that:

1. Parses every enum field via protobuf's `_Parse()` function
2. Sets every scalar / repeated / nested field on the proto message
3. Returns a descriptive error on the first invalid value
4. On success, stores the JSON; on failure, keeps the old config

This ensures the stored JSON can always be converted to a valid protobuf.
