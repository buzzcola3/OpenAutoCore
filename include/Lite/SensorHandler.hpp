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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <Common/ChannelId.hpp>
#include <Common/EncryptionType.hpp>
#include <Common/MessageType.hpp>

namespace google::protobuf { class MessageLite; }
#include <nlohmann/json_fwd.hpp>

namespace aasdk::lite {

struct InMessage;
using SendFn = std::function<void(messenger::ChannelId,
                                  messenger::EncryptionType,
                                  messenger::MessageType,
                                  const uint8_t*, size_t)>;

/// Lite-stack handler for SENSOR channel.
class SensorHandler {
public:
    explicit SensorHandler(SendFn sender);

    /// ChannelRouter-compatible inbound callback.
    void operator()(const InMessage& msg);

    /// Called from transport when a sensor JSON event arrives (HU → Phone).
    void onSensorEvent(uint64_t timestamp, const void* data, size_t size);

private:
    void handleChannelOpenRequest(const InMessage& msg,
                                  const uint8_t* data, size_t size);
    void handleSensorStartRequest(const InMessage& msg,
                                  const uint8_t* data, size_t size);
    void handleSensorStopRequest(const InMessage& msg,
                                 const uint8_t* data, size_t size);

    void sendLocationIndication(const nlohmann::json& location);
    void sendNightModeIndication(const nlohmann::json& nightMode);
    void sendDrivingStatusIndication(const nlohmann::json& drivingStatus);

    void sendProto(messenger::ChannelId ch,
                   messenger::EncryptionType enc,
                   messenger::MessageType mt,
                   uint16_t messageId,
                   const google::protobuf::MessageLite& proto);

    SendFn send_;
    messenger::ChannelId sensorChannelId_{messenger::ChannelId::NONE};
    messenger::EncryptionType sensorEncryptionType_{messenger::EncryptionType::PLAIN};
    uint64_t messageCount_{0};
};

} // namespace aasdk::lite
