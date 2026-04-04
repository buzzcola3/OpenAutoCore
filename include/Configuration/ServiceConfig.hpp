#pragma once

#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>

namespace f1x::openauto::autoapp::configuration {

class ServiceConfig {
public:
    ServiceConfig(std::string defaultPath, std::string userPath);

    /// Load config from default JSON + optional user JSON overlay.
    bool load();

    /// Save current config to user JSON file.
    bool save();

    /// Delete user JSON and reload from defaults only.
    bool reset();

    /// Return current config as a JSON string (pretty-printed).
    std::string getJson() const;

    /// Replace config from a JSON string.
    /// Validates by building the proto — if construction fails the config
    /// is rejected and the previous state is kept.
    /// Returns empty string on success, error description on failure.
    std::string setJson(const std::string& jsonStr);

    /// Build the ServiceDiscoveryResponse protobuf from current JSON.
    aap_protobuf::service::control::message::ServiceDiscoveryResponse toProto() const;

    /// Build a textproto string from current JSON.
    std::string toTextProto() const;

private:
    /// Try to construct a ServiceDiscoveryResponse from the given JSON.
    /// Returns empty string on success, error description on failure.
    static std::string buildProto(
        const nlohmann::json& j,
        aap_protobuf::service::control::message::ServiceDiscoveryResponse& out);

    std::string defaultPath_;
    std::string userPath_;
    mutable std::mutex mu_;
    nlohmann::json config_;
};

} // namespace f1x::openauto::autoapp::configuration
