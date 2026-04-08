# ServiceConfig

JSON-to-protobuf converter for the Android Auto `ServiceDiscoveryResponse`.

## API

Header: `include/Configuration/ServiceConfig.hpp`
Implementation: `src/Configuration/ServiceConfig.cpp`

```cpp
class ServiceConfig {
public:
    ServiceConfig() = default;

    std::string getJson() const;             // Current config as pretty-printed JSON
    std::string setJson(const std::string&); // Validate + replace; returns "" on success, error on failure

    ServiceDiscoveryResponse toProto() const; // Build protobuf from current JSON
    std::string toTextProto() const;          // Build textproto string
};
```

## How It Works

1. Core starts with an empty ServiceConfig
2. Core sends `{"action":"request_config"}` to the frontend over `MsgType::CONFIGURATION`
3. Frontend responds with the full config JSON (raw `{"channels":[...]}`)
4. Core calls `setJson()` which validates by building the proto — if invalid, rejects with error
5. `ControlHandler::handleServiceDiscovery()` calls `toProto()` to get the protobuf for the phone

## Validation

`setJson()` calls `buildProto()` which maps every field into a `ServiceDiscoveryResponse`
protobuf. Each enum value is validated via proto-generated `_Parse()`. On failure, the
previous config is kept and a descriptive error is returned.

## Thread Safety

All public methods are protected by `std::mutex`.

## Dependencies

- `nlohmann_json` — JSON parsing
- `google::protobuf::TextFormat` — textproto output
- Proto-generated `_Parse()` functions — enum validation