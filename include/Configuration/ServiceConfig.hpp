// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>

namespace f1x::openauto::autoapp::configuration {

class ServiceConfig {
public:
    ServiceConfig() = default;

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

    mutable std::mutex mu_;
    nlohmann::json config_;
};

} // namespace f1x::openauto::autoapp::configuration
