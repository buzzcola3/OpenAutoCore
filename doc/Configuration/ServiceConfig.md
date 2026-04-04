# ServiceConfig Interface

This document describes the implemented ServiceConfig system in OpenAutoCore.

## Overview

ServiceConfig provides a simplified JSON-backed configuration for the Android Auto
`ServiceDiscoveryResponse`. The frontend interacts via three RPC actions (`get`, `set`,
`reset`) over `MsgType::configuration`. Validation is performed by constructing the
protobuf from JSON — invalid enum values or structural errors are caught and reported.

## ServiceConfig API

Header: `include/Configuration/ServiceConfig.hpp`
Implementation: `src/Configuration/ServiceConfig.cpp`

```cpp
class ServiceConfig {
public:
    ServiceConfig(std::string defaultPath, std::string userPath);

    bool load();                            // Load default JSON + optional user overlay
    bool save();                            // Persist current config to user JSON file
    bool reset();                           // Delete user file, reload from defaults

    std::string getJson() const;            // Current config as pretty-printed JSON
    std::string setJson(const std::string&); // Validate + replace; returns "" on success, error on failure

    ServiceDiscoveryResponse toProto() const; // Build protobuf from current JSON
    std::string toTextProto() const;          // Build textproto string via toProto()
};
```

### Lifecycle

1. **`load()`** — Reads `ServiceDiscoveryResponse.default.json`. If a user override
   file exists, it replaces the defaults entirely. Validates by calling `buildProto()`.

2. **`save()`** — Writes the current JSON to the user config file.

3. **`reset()`** — Deletes the user config file and reloads from defaults only.

### Validation

`setJson()` parses the candidate JSON, then calls `buildProto()` which maps every
field into a `ServiceDiscoveryResponse` protobuf. Each enum value is validated via
the proto-generated `_Parse()` function. On failure, the previous config is kept
and a descriptive error string is returned (e.g. `"invalid driver_position value: 'BAD'"`).

### Thread Safety

All public methods are protected by a `std::mutex`. ServiceConfig is safely shared
between the transport RPC handler and `AndroidAutoEntity`.

## RPC Protocol

JSON over `MsgType::configuration` (requires OpenAutoTransport v0.0.26).

### Request Format

```json
{"id": 1, "action": "get"}
{"id": 2, "action": "set", "config": { ... }}
{"id": 3, "action": "reset"}
```

### Response Format

```json
{"id": 1, "ok": true, "config": { ... }}
{"id": 2, "ok": true}
{"id": 2, "ok": false, "error": "invalid driver_position value: 'BAD'"}
{"id": 3, "ok": true, "config": { ... }}
```

## Wiring

- `autoapp.cpp` creates a `ServiceConfig` and calls `load()` at startup
- `ServiceConfig&` is passed through `AndroidAutoEntityFactory` to `AndroidAutoEntity`
- `AndroidAutoEntity::onServiceDiscoveryRequest()` calls `serviceConfig_.toProto()`
- The transport RPC handler (pending `MsgType::configuration`) dispatches
  `getJson()`, `setJson()`, and `reset()` calls

## Config Files

| File | Purpose |
|------|---------|
| `configuration/ServiceDiscoveryResponse.default.json` | Shipped defaults (checked in, read-only) |
| `configuration/UserServiceDiscoveryResponse.json` | User override (gitignored, written by `save()`) |

## Dependencies

- `nlohmann_json` — JSON parsing and serialization
- `google::protobuf::TextFormat` — textproto output
- Proto-generated `_Parse()` functions — enum validation