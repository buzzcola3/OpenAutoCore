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

#include <Lite/NavigationStatusHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Common/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/navigationstatus/NavigationStatusMessageId.pb.h>
#include <aap_protobuf/service/navigationstatus/message/NavigationCurrentPosition.pb.h>
#include <aap_protobuf/service/navigationstatus/message/NavigationStatus.pb.h>
#include <aap_protobuf/service/navigationstatus/message/NavigationStatusStart.pb.h>
#include <aap_protobuf/service/navigationstatus/message/NavigationStatusStop.pb.h>
#include <aap_protobuf/service/navigationstatus/message/NavigationState.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {
using Control = aap_protobuf::service::control::message::ControlMessageType;
using Navigation = aap_protobuf::service::navigationstatus::NavigationStatusMessageId;
constexpr const char* kTag = "[LiteNavigationStatus]";
}

namespace aasdk::lite {

NavigationStatusHandler::NavigationStatusHandler(SendFn sender)
    : send_(std::move(sender)) {}

void NavigationStatusHandler::operator()(const InMessage& msg) {
    ++messageCount_;
    const auto& payload = msg.payload;
    if (payload.size() < 2) {
        AASDK_LOG(error) << kTag << " payload too small";
        return;
    }
    uint16_t msgId = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    const uint8_t* data = payload.data() + 2;
    size_t size = payload.size() - 2;

    switch (msgId) {
        case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
            handleChannelOpenRequest(msg, data, size);
            break;
        case Navigation::INSTRUMENT_CLUSTER_NAVIGATION_STATUS: {
            aap_protobuf::service::navigationstatus::message::NavigationStatus status;
            if (status.ParseFromArray(data, static_cast<int>(size)))
                AASDK_LOG(debug) << kTag << " NavigationStatus: " << status.ShortDebugString();
            break;
        }
        case Navigation::INSTRUMENT_CLUSTER_NAVIGATION_STATE: {
            aap_protobuf::service::navigationstatus::message::NavigationState state;
            if (state.ParseFromArray(data, static_cast<int>(size)))
                AASDK_LOG(debug) << kTag << " NavigationState: " << state.ShortDebugString();
            break;
        }
        case Navigation::INSTRUMENT_CLUSTER_NAVIGATION_CURRENT_POSITION: {
            aap_protobuf::service::navigationstatus::message::NavigationCurrentPosition pos;
            if (pos.ParseFromArray(data, static_cast<int>(size)))
                AASDK_LOG(debug) << kTag << " NavigationCurrentPosition: " << pos.ShortDebugString();
            break;
        }
        case Navigation::INSTRUMENT_CLUSTER_START: {
            aap_protobuf::service::navigationstatus::message::NavigationStatusStart start;
            if (start.ParseFromArray(data, static_cast<int>(size)))
                AASDK_LOG(debug) << kTag << " NavigationStatusStart: " << start.ShortDebugString();
            break;
        }
        case Navigation::INSTRUMENT_CLUSTER_STOP: {
            aap_protobuf::service::navigationstatus::message::NavigationStatusStop stop;
            if (stop.ParseFromArray(data, static_cast<int>(size)))
                AASDK_LOG(debug) << kTag << " NavigationStatusStop: " << stop.ShortDebugString();
            break;
        }
        default:
            AASDK_LOG(debug) << kTag << " unhandled msgId=" << msgId;
            break;
    }
}

void NavigationStatusHandler::handleChannelOpenRequest(const InMessage& msg,
                                                       const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ChannelOpenRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse ChannelOpenRequest";
        return;
    }
    AASDK_LOG(debug) << kTag << " ChannelOpenRequest: " << request.ShortDebugString();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::CONTROL,
              Control::MESSAGE_CHANNEL_OPEN_RESPONSE, response);
}

void NavigationStatusHandler::sendProto(messenger::ChannelId ch,
                                        messenger::EncryptionType enc,
                                        messenger::MessageType mt,
                                        uint16_t messageId,
                                        const google::protobuf::MessageLite& proto) {
    size_t protoSize = proto.ByteSizeLong();
    std::vector<uint8_t> buf(2 + protoSize);
    buf[0] = static_cast<uint8_t>(messageId >> 8);
    buf[1] = static_cast<uint8_t>(messageId & 0xFF);
    proto.SerializeToArray(buf.data() + 2, static_cast<int>(protoSize));
    send_(ch, enc, mt, buf.data(), buf.size());
}

} // namespace aasdk::lite
