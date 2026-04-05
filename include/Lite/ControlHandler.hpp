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
#include <string>
#include <vector>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Data.hpp>
#include <aap_protobuf/service/control/message/AudioFocusRequest.pb.h>
#include <aap_protobuf/service/control/message/BatteryStatusNotification.pb.h>
#include <aap_protobuf/service/control/message/ByeByeRequest.pb.h>
#include <aap_protobuf/service/control/message/ByeByeResponse.pb.h>
#include <aap_protobuf/service/control/message/NavFocusRequestNotification.pb.h>
#include <aap_protobuf/service/control/message/PingRequest.pb.h>
#include <aap_protobuf/service/control/message/PingResponse.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryRequest.pb.h>
#include <aap_protobuf/service/control/message/VoiceSessionNotification.pb.h>
#include <aap_protobuf/service/control/message/AuthResponse.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusNotification.pb.h>
#include <aap_protobuf/service/control/message/NavFocusNotification.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

namespace aasdk::lite {

struct InMessage;
using SendFn = std::function<void(messenger::ChannelId,
                                  messenger::EncryptionType,
                                  messenger::MessageType,
                                  const uint8_t*, size_t)>;

class ControlHandler {
public:
    explicit ControlHandler(SendFn sender);
    void operator()(const InMessage& msg);

    // --- Outbound sends (called by AndroidAutoEntity) ---
    void sendVersionRequest();
    void sendHandshake(const common::Data& buffer);
    void sendAuthComplete(const aap_protobuf::service::control::message::AuthResponse& response);
    void sendServiceDiscoveryResponse(const aap_protobuf::service::control::message::ServiceDiscoveryResponse& response);
    void sendAudioFocusResponse(const aap_protobuf::service::control::message::AudioFocusNotification& response);
    void sendNavigationFocusResponse(const aap_protobuf::service::control::message::NavFocusNotification& response);
    void sendShutdownRequest(const aap_protobuf::service::control::message::ByeByeRequest& request);
    void sendShutdownResponse(const aap_protobuf::service::control::message::ByeByeResponse& response);
    void sendPingRequest(const aap_protobuf::service::control::message::PingRequest& request);
    void sendPingResponse(const aap_protobuf::service::control::message::PingResponse& response);

    // --- Inbound callbacks (set by AndroidAutoEntity) ---
    std::function<void(uint16_t, uint16_t, aap_protobuf::shared::MessageStatus)> onVersionResponse;
    std::function<void(const common::DataConstBuffer&)> onHandshake;
    std::function<void(const aap_protobuf::service::control::message::ServiceDiscoveryRequest&)> onServiceDiscoveryRequest;
    std::function<void(const aap_protobuf::service::control::message::AudioFocusRequest&)> onAudioFocusRequest;
    std::function<void(const aap_protobuf::service::control::message::NavFocusRequestNotification&)> onNavigationFocusRequest;
    std::function<void(const aap_protobuf::service::control::message::ByeByeRequest&)> onByeByeRequest;
    std::function<void(const aap_protobuf::service::control::message::ByeByeResponse&)> onByeByeResponse;
    std::function<void(const aap_protobuf::service::control::message::BatteryStatusNotification&)> onBatteryStatusNotification;
    std::function<void(const aap_protobuf::service::control::message::VoiceSessionNotification&)> onVoiceSessionRequest;
    std::function<void(const aap_protobuf::service::control::message::PingRequest&)> onPingRequest;
    std::function<void(const aap_protobuf::service::control::message::PingResponse&)> onPingResponse;
    std::function<void(uint16_t)> onChannelOpenRequest;

private:
    void sendRaw(uint16_t messageId, messenger::EncryptionType enc,
                 const uint8_t* data, size_t size);
    void sendProto(uint16_t messageId, messenger::EncryptionType enc,
                   const google::protobuf::MessageLite& proto);

    SendFn send_;
};

} // namespace aasdk::lite
