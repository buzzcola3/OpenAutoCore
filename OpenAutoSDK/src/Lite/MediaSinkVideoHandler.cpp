#include <Lite/MediaSinkVideoHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Messenger/MessageType.hpp>
#include <Messenger/Timestamp.hpp>
#include <Common/Log.hpp>
#include <open_auto_transport/transport.hpp>
#include <open_auto_transport/wire.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>
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
#include <aap_protobuf/service/media/video/message/VideoFocusNotification.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusRequestNotification.pb.h>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Media   = aap_protobuf::service::media::sink::MediaMessageId;

} // namespace

namespace aasdk::lite {

MediaSinkVideoHandler::MediaSinkVideoHandler(
    SendFn sender,
    std::shared_ptr<buzz::autoapp::Transport::Transport> transport)
    : send_(std::move(sender)), transport_(std::move(transport)) {}

void MediaSinkVideoHandler::operator()(const InMessage& msg) {
    ++messageCount_;
    const auto& payload = msg.payload;

    if (payload.size() < 2) {
        AASDK_LOG(error) << "[LiteMediaSinkVideo] payload too small";
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
                AASDK_LOG(debug) << "[LiteMediaSinkVideo] MediaStart: session=" << sessionId_;
            }
            break;
        }
        case Media::MEDIA_MESSAGE_STOP: {
            aap_protobuf::service::media::shared::message::Stop stop;
            if (stop.ParseFromArray(data, static_cast<int>(size))) {
                AASDK_LOG(debug) << "[LiteMediaSinkVideo] MediaStop: " << stop.ShortDebugString();
            }
            break;
        }
        case Media::MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST: {
            aap_protobuf::service::media::video::message::VideoFocusRequestNotification req;
            if (req.ParseFromArray(data, static_cast<int>(size))) {
                AASDK_LOG(debug) << "[LiteMediaSinkVideo] VideoFocusRequest: " << req.ShortDebugString();
            }
            break;
        }
        case Media::MEDIA_MESSAGE_CODEC_CONFIG:
            handleCodecConfig(msg, data, size);
            break;
        case Media::MEDIA_MESSAGE_DATA:
            handleMediaData(msg, data, size);
            break;
        default:
            AASDK_LOG(debug) << "[LiteMediaSinkVideo] unhandled msgId=" << msgId;
            break;
    }
}

void MediaSinkVideoHandler::handleChannelOpenRequest(const InMessage& msg,
                                                     const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ChannelOpenRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteMediaSinkVideo] Failed to parse ChannelOpenRequest";
        return;
    }
    AASDK_LOG(debug) << "[LiteMediaSinkVideo] ChannelOpenRequest: " << request.ShortDebugString();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::CONTROL,
              Control::MESSAGE_CHANNEL_OPEN_RESPONSE, response);
}

void MediaSinkVideoHandler::handleMediaSetup(const InMessage& msg,
                                              const uint8_t* data, size_t size) {
    aap_protobuf::service::media::shared::message::Setup setup;
    if (!setup.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteMediaSinkVideo] Failed to parse MediaSetup";
        return;
    }
    AASDK_LOG(info) << "[LiteMediaSinkVideo] MediaSetup: codec="
                    << aap_protobuf::service::media::shared::message::MediaCodecType_Name(setup.type());

    // Send Config response
    aap_protobuf::service::media::shared::message::Config config;
    config.set_status(aap_protobuf::service::media::shared::message::Config::STATUS_READY);
    config.set_max_unacked(1);
    config.add_configuration_indices(0);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_CONFIG, config);

    // Send VideoFocusNotification
    aap_protobuf::service::media::video::message::VideoFocusNotification focus;
    focus.set_focus(aap_protobuf::service::media::video::message::VideoFocusMode::VIDEO_FOCUS_PROJECTED);
    focus.set_unsolicited(false);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION, focus);
}

void MediaSinkVideoHandler::handleCodecConfig(const InMessage& msg,
                                              const uint8_t* data, size_t size) {
    AASDK_LOG(debug) << "[LiteMediaSinkVideo] codec config blob size=" << size;

    if (sessionId_ < 0) {
        AASDK_LOG(error) << "[LiteMediaSinkVideo] session not set, cannot ACK codec config";
        return;
    }

    if (ensureTransportStarted()) {
        transport_->send(buzz::wire::MsgType::VIDEO, 0, data, size);
    }

    aap_protobuf::service::media::source::message::Ack ack;
    ack.set_session_id(sessionId_);
    ack.set_ack(1);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_ACK, ack);
}

void MediaSinkVideoHandler::handleMediaData(const InMessage& msg,
                                            const uint8_t* data, size_t size) {
    if (sessionId_ < 0) {
        AASDK_LOG(error) << "[LiteMediaSinkVideo] session not set, cannot ACK media data";
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
        transport_->send(buzz::wire::MsgType::VIDEO, timestamp, frameData, frameSize);
    }

    aap_protobuf::service::media::source::message::Ack ack;
    ack.set_session_id(sessionId_);
    ack.set_ack(1);
    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Media::MEDIA_MESSAGE_ACK, ack);
}

void MediaSinkVideoHandler::sendProto(messenger::ChannelId ch,
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

bool MediaSinkVideoHandler::ensureTransportStarted() {
    if (transport_ && transport_->isRunning()) return true;

    if (!transport_) {
        transport_ = std::make_shared<buzz::autoapp::Transport::Transport>();
    }

    if (!transport_->startAsA(std::chrono::microseconds{1000})) {
        AASDK_LOG(error) << "[LiteMediaSinkVideo] Failed to start transport";
        return false;
    }
    return transport_->isRunning();
}

uint64_t MediaSinkVideoHandler::resolveTimestamp(bool hasTimestamp, uint64_t parsedTs) const {
    if (hasTimestamp) return parsedTs;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

} // namespace aasdk::lite
