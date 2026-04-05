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

#include <Lite/MediaSourceHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Messenger/MessageType.hpp>
#include <Messenger/Timestamp.hpp>
#include <Common/Data.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/media/sink/MediaMessageId.pb.h>
#include <aap_protobuf/service/media/shared/message/Setup.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/source/message/MicrophoneRequest.pb.h>
#include <aap_protobuf/service/media/source/message/MicrophoneResponse.pb.h>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {
using Control = aap_protobuf::service::control::message::ControlMessageType;
using Media = aap_protobuf::service::media::sink::MediaMessageId;
constexpr const char* kTag = "[LiteMediaSource]";
}

namespace aasdk::lite {

MediaSourceHandler::MediaSourceHandler(SendFn sender)
    : send_(std::move(sender)) {}

void MediaSourceHandler::operator()(const InMessage& msg) {
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
        case Media::MEDIA_MESSAGE_SETUP:
            handleMediaChannelSetupRequest(msg, data, size);
            break;
        case Media::MEDIA_MESSAGE_MICROPHONE_REQUEST:
            handleMicrophoneRequest(msg, data, size);
            break;
        case Media::MEDIA_MESSAGE_ACK:
            handleMediaChannelAck(data, size);
            break;
        default:
            AASDK_LOG(debug) << kTag << " unhandled msgId=" << msgId;
            break;
    }
}

void MediaSourceHandler::handleChannelOpenRequest(const InMessage& msg,
                                                  const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ChannelOpenRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse ChannelOpenRequest";
        return;
    }
    AASDK_LOG(debug) << kTag << " ChannelOpenRequest: " << request.ShortDebugString();

    mediaSourceChannelId_ = msg.channelId;
    mediaSourceEncryptionType_ = msg.encryptionType;
    channelOpen_ = true;

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::CONTROL,
              Control::MESSAGE_CHANNEL_OPEN_RESPONSE, response);
}

void MediaSourceHandler::handleMediaChannelSetupRequest(const InMessage& msg,
                                                        const uint8_t* data, size_t size) {
    aap_protobuf::service::media::shared::message::Setup request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse MediaSetup";
        return;
    }
    AASDK_LOG(debug) << kTag << " MediaSetup: " << request.ShortDebugString();

    aap_protobuf::service::media::shared::message::Config response;
    response.set_status(aap_protobuf::service::media::shared::message::Config::STATUS_READY);
    response.set_max_unacked(1);
    response.add_configuration_indices(0);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_SETUP, response);
}

void MediaSourceHandler::handleMicrophoneRequest(const InMessage& msg,
                                                 const uint8_t* data, size_t size) {
    aap_protobuf::service::media::source::message::MicrophoneRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse MicrophoneRequest";
        return;
    }
    AASDK_LOG(debug) << kTag << " MicrophoneRequest: " << request.ShortDebugString();

    microphoneEnabled_ = request.open();
    if (!request.open()) {
        sessionId_ = 0;
    }

    aap_protobuf::service::media::source::message::MicrophoneResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
    response.set_session_id(sessionId_);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_MICROPHONE_REQUEST, response);
}

void MediaSourceHandler::handleMediaChannelAck(const uint8_t* data, size_t size) {
    aap_protobuf::service::media::source::message::Ack indication;
    if (!indication.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << kTag << " Failed to parse MediaAck";
        return;
    }
    AASDK_LOG(debug) << kTag << " MediaAck: " << indication.ShortDebugString();
}

void MediaSourceHandler::onMicrophoneAudio(uint64_t timestamp,
                                           const void* data, size_t size) {
    if (!channelOpen_) {
        AASDK_LOG(error) << kTag << " Channel not open; dropping microphone audio.";
        return;
    }
    if (!microphoneEnabled_) {
        AASDK_LOG(debug) << kTag << " Microphone not enabled; dropping audio.";
        return;
    }
    if (data == nullptr || size == 0) {
        AASDK_LOG(error) << kTag << " Microphone audio payload invalid.";
        return;
    }

    auto timestampData = messenger::Timestamp(timestamp).getData();

    // Build [2-byte msgId][timestamp][audio data] buffer
    uint16_t msgId = Media::MEDIA_MESSAGE_DATA;
    std::vector<uint8_t> buf;
    buf.reserve(2 + timestampData.size() + size);
    buf.push_back(static_cast<uint8_t>(msgId >> 8));
    buf.push_back(static_cast<uint8_t>(msgId & 0xFF));
    buf.insert(buf.end(), timestampData.begin(), timestampData.end());
    buf.insert(buf.end(),
               static_cast<const uint8_t*>(data),
               static_cast<const uint8_t*>(data) + size);

    send_(mediaSourceChannelId_, mediaSourceEncryptionType_,
          messenger::MessageType::SPECIFIC, buf.data(), buf.size());
}

void MediaSourceHandler::sendProto(messenger::ChannelId ch,
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
