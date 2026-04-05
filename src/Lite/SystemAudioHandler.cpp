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

#include <Lite/SystemAudioHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Messenger/MessageType.hpp>
#include <Messenger/Timestamp.hpp>
#include <Common/Log.hpp>
#include <open_auto_transport/transport.hpp>
#include <open_auto_transport/wire.hpp>
#include <chrono>
#include <cstdint>
#include <limits>

#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/shared/message/Setup.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/media/shared/message/Start.pb.h>
#include <aap_protobuf/service/media/shared/message/Stop.pb.h>
#include <aap_protobuf/service/media/sink/MediaMessageId.pb.h>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Media   = aap_protobuf::service::media::sink::MediaMessageId;

} // namespace

namespace aasdk::lite {

SystemAudioHandler::SystemAudioHandler(
    SendFn sender,
    std::shared_ptr<buzz::autoapp::Transport::Transport> transport)
    : send_(std::move(sender)), transport_(std::move(transport)) {}

void SystemAudioHandler::operator()(const InMessage& msg) {
    ++messageCount_;
    const auto& payload = msg.payload;

    if (payload.size() < 2) {
        AASDK_LOG(error) << "[LiteSystemAudio] payload too small";
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
            handleMediaSetup(msg, data, size);
            break;
        case Media::MEDIA_MESSAGE_START: {
            aap_protobuf::service::media::shared::message::Start start;
            if (start.ParseFromArray(data, static_cast<int>(size))) {
                sessionId_ = start.session_id();
                AASDK_LOG(debug) << "[LiteSystemAudio] MediaStart: session=" << sessionId_;
            }
            break;
        }
        case Media::MEDIA_MESSAGE_STOP: {
            aap_protobuf::service::media::shared::message::Stop stop;
            if (stop.ParseFromArray(data, static_cast<int>(size))) {
                AASDK_LOG(debug) << "[LiteSystemAudio] MediaStop: " << stop.ShortDebugString();
            }
            break;
        }
        case Media::MEDIA_MESSAGE_CODEC_CONFIG:
            handleCodecConfig(msg, data, size);
            break;
        case Media::MEDIA_MESSAGE_DATA:
            handleMediaData(msg, data, size);
            break;
        case Media::MEDIA_MESSAGE_AUDIO_UNDERFLOW_NOTIFICATION:
            AASDK_LOG(warning) << "[LiteSystemAudio] Audio underflow notification";
            break;
        default:
            AASDK_LOG(debug) << "[LiteSystemAudio] unhandled msgId=" << msgId;
            break;
    }
}

void SystemAudioHandler::handleChannelOpenRequest(const InMessage& msg,
                                                  const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ChannelOpenRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteSystemAudio] Failed to parse ChannelOpenRequest";
        return;
    }
    AASDK_LOG(debug) << "[LiteSystemAudio] ChannelOpenRequest: " << request.ShortDebugString();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::CONTROL,
              Control::MESSAGE_CHANNEL_OPEN_RESPONSE, response);
}

void SystemAudioHandler::handleMediaSetup(const InMessage& msg,
                                          const uint8_t* data, size_t size) {
    aap_protobuf::service::media::shared::message::Setup setup;
    if (!setup.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteSystemAudio] Failed to parse MediaSetup";
        return;
    }
    AASDK_LOG(info) << "[LiteSystemAudio] MediaSetup: codec="
                    << aap_protobuf::service::media::shared::message::MediaCodecType_Name(setup.type());

    aap_protobuf::service::media::shared::message::Config config;
    config.set_status(aap_protobuf::service::media::shared::message::Config::STATUS_READY);
    config.set_max_unacked(1);
    config.add_configuration_indices(0);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_CONFIG, config);

    AASDK_LOG(debug) << "[LiteSystemAudio] MediaSetup response: " << config.ShortDebugString();
}

void SystemAudioHandler::handleCodecConfig(const InMessage& msg,
                                           const uint8_t* data, size_t size) {
    AASDK_LOG(debug) << "[LiteSystemAudio] codec config blob size=" << size;

    if (sessionId_ < 0) {
        AASDK_LOG(error) << "[LiteSystemAudio] session not set, cannot ACK codec config";
        return;
    }

    if (ensureTransportStarted()) {
        transport_->send(buzz::wire::MsgType::SYSTEM_AUDIO, 0, data, size);
    }

    aap_protobuf::service::media::source::message::Ack ack;
    ack.set_session_id(sessionId_);
    ack.set_ack(1);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_ACK, ack);
}

void SystemAudioHandler::handleMediaData(const InMessage& msg,
                                         const uint8_t* data, size_t size) {
    if (sessionId_ < 0) {
        AASDK_LOG(error) << "[LiteSystemAudio] session not set, cannot ACK media data";
        return;
    }

    constexpr auto timestampBytes = sizeof(uint64_t);
    const bool hasTimestamp = size >= timestampBytes;
    const uint8_t* frameData = data;
    size_t frameSize = size;
    uint64_t timestamp = 0;

    if (hasTimestamp) {
        messenger::Timestamp ts(common::DataConstBuffer(data, timestampBytes));
        timestamp = resolveTimestamp(true, ts.getValue());
        frameData += timestampBytes;
        frameSize -= timestampBytes;
    } else {
        timestamp = resolveTimestamp(false, 0);
    }

    if (ensureTransportStarted()) {
        transport_->send(buzz::wire::MsgType::SYSTEM_AUDIO, timestamp, frameData, frameSize);
    }

    aap_protobuf::service::media::source::message::Ack ack;
    ack.set_session_id(sessionId_);
    ack.set_ack(1);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_ACK, ack);
}

void SystemAudioHandler::sendProto(messenger::ChannelId ch,
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

bool SystemAudioHandler::ensureTransportStarted() {
    if (transport_ && transport_->isRunning()) return true;
    if (!transport_) {
        transport_ = std::make_shared<buzz::autoapp::Transport::Transport>();
    }
    if (!transport_->startAsA(std::chrono::microseconds{1000})) {
        AASDK_LOG(error) << "[LiteSystemAudio] Failed to start transport";
        return false;
    }
    return transport_->isRunning();
}

uint64_t SystemAudioHandler::resolveTimestamp(bool hasTimestamp, uint64_t parsedTs) const {
    if (hasTimestamp) return parsedTs;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

} // namespace aasdk::lite
