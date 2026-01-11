#include <Messenger/handlers/GuidanceAudioMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/ChannelId.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Messenger/Timestamp.hpp>
#include <Common/Log.hpp>
#include <open_auto_transport/transport.hpp>
#include <open_auto_transport/wire.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

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
using Media = aap_protobuf::service::media::sink::MediaMessageId;
constexpr const char* kLogPrefix = "[GuidanceAudioMessageHandlers]";

} // namespace

namespace aasdk::messenger::interceptor {

bool GuidanceAudioMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " media guidance payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  bool handled = false;
  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      handled = handleChannelOpenRequest(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_SETUP:
      handled = handleChannelSetupRequest(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_START: {
      aap_protobuf::service::media::shared::message::Start start;
      if (start.ParseFromArray(payloadData, static_cast<int>(payloadSize))) {
        sessionId_ = start.session_id();
        AASDK_LOG(debug) << kLogPrefix << " MediaStart: session=" << sessionId_;
      } else {
        AASDK_LOG(error) << kLogPrefix << " Failed to parse MediaStart payload";
      }
      break;
    }
    case Media::MEDIA_MESSAGE_STOP: {
      aap_protobuf::service::media::shared::message::Stop stop;
      if (stop.ParseFromArray(payloadData, static_cast<int>(payloadSize))) {
        AASDK_LOG(debug) << kLogPrefix << " MediaStop: " << stop.ShortDebugString();
      } else {
        AASDK_LOG(error) << kLogPrefix << " Failed to parse MediaStop payload";
      }
      break;
    }
    case Media::MEDIA_MESSAGE_CODEC_CONFIG:
      handled = handleCodecConfig(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_DATA:
      handled = handleMediaData(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_AUDIO_UNDERFLOW_NOTIFICATION:
      AASDK_LOG(warning) << kLogPrefix << " Audio underflow notification received.";
      handled = true;
      break;
    default:
      AASDK_LOG(debug) << kLogPrefix << " guidance audio message id=" << messageId.getId()
                       << " not explicitly decoded.";
      break;
  }

  return handled;
}

bool GuidanceAudioMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                             const std::uint8_t* data,
                                                             std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!request.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << kLogPrefix << " Failed to parse ChannelOpenRequest payload";
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  AASDK_LOG(debug) << kLogPrefix << " Constructed ChannelOpenResponse: "
                   << response.ShortDebugString();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send response.";
  return false;
}

bool GuidanceAudioMessageHandlers::handleMediaData(const ::aasdk::messenger::Message& message,
                                                    const std::uint8_t* data,
                                                    std::size_t size) const {
  if (sender_ == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send media ACK.";
    return false;
  }

  if (sessionId_ < 0) {
    AASDK_LOG(error) << kLogPrefix << " Session id not set; cannot send media ACK.";
    return false;
  }

  const bool hasTimestamp = size >= sizeof(::aasdk::messenger::Timestamp::ValueType);
  constexpr auto timestampBytes = sizeof(::aasdk::messenger::Timestamp::ValueType);
  const std::uint8_t* frameData = data;
  std::size_t frameSize = size;

  uint64_t timestamp = 0;
  if (hasTimestamp) {
    ::aasdk::messenger::Timestamp ts(common::DataConstBuffer(data, timestampBytes));
    timestamp = resolveTimestamp(true, ts.getValue());
    frameData += timestampBytes;
    frameSize -= timestampBytes;
  } else {
    timestamp = resolveTimestamp(false, 0);
  }

  if (ensureTransportStarted()) {
    transport_->send(buzz::wire::MsgType::GUIDANCE_AUDIO, timestamp, frameData, frameSize);
  }

  aap_protobuf::service::media::source::message::Ack ack;
  ack.set_session_id(sessionId_);
  ack.set_ack(1);

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        aap_protobuf::service::media::sink::MediaMessageId::MEDIA_MESSAGE_ACK,
                        ack);

  return true;
}

bool GuidanceAudioMessageHandlers::handleChannelSetupRequest(const ::aasdk::messenger::Message& message,
                                                              const std::uint8_t* data,
                                                              std::size_t size) const {
  aap_protobuf::service::media::shared::message::Setup setup;
  if (!setup.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << kLogPrefix << " Failed to parse MediaSetup payload";
    return false;
  }

  AASDK_LOG(info) << kLogPrefix << " MediaSetup: channel="
                  << channelIdToString(message.getChannelId())
                  << ", codec=" << aap_protobuf::service::media::shared::message::MediaCodecType_Name(setup.type());

  aap_protobuf::service::media::shared::message::Config response;
  response.set_status(aap_protobuf::service::media::shared::message::Config::STATUS_READY);
  response.set_max_unacked(1);
  response.add_configuration_indices(0);

  if (sender_ != nullptr) {
    sender_->sendProtobuf(
        message.getChannelId(),
        message.getEncryptionType(),
        ::aasdk::messenger::MessageType::SPECIFIC,
        aap_protobuf::service::media::sink::MediaMessageId::MEDIA_MESSAGE_CONFIG,
        response);

    AASDK_LOG(debug) << kLogPrefix << " MediaSetup response: " << response.ShortDebugString();
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send setup response.";
  return false;
}

bool GuidanceAudioMessageHandlers::handleCodecConfig(const ::aasdk::messenger::Message& message,
                                                      const std::uint8_t* data,
                                                      std::size_t size) const {
  AASDK_LOG(debug) << kLogPrefix << " codec configuration blob size=" << size
                   << " bytes on channel " << channelIdToString(message.getChannelId());

  if (sender_ == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send media ACK.";
    return false;
  }

  if (sessionId_ < 0) {
    AASDK_LOG(error) << kLogPrefix << " Session id not set; cannot send media ACK.";
    return false;
  }

  if (ensureTransportStarted()) {
    transport_->send(buzz::wire::MsgType::GUIDANCE_AUDIO, 0, data, size);
  }

  aap_protobuf::service::media::source::message::Ack ack;
  ack.set_session_id(sessionId_);
  ack.set_ack(1);

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        aap_protobuf::service::media::sink::MediaMessageId::MEDIA_MESSAGE_ACK,
                        ack);

  return true;
}

void GuidanceAudioMessageHandlers::setMessageSender(
    std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

void GuidanceAudioMessageHandlers::setTransport(
    std::shared_ptr<buzz::autoapp::Transport::Transport> transport) {
  transport_ = std::move(transport);
}

bool GuidanceAudioMessageHandlers::ensureTransportStarted() const {
  if (transport_ && transport_->isRunning()) {
    return true;
  }

  if (!transport_) {
    transport_ = std::make_shared<buzz::autoapp::Transport::Transport>();
  }

  if (!transport_->startAsA(std::chrono::microseconds{1000})) {
    AASDK_LOG(error) << kLogPrefix << " Failed to start OpenAutoTransport (side A).";
    return false;
  }

  return transport_->isRunning();
}

uint64_t GuidanceAudioMessageHandlers::resolveTimestamp(bool hasTimestamp, uint64_t parsedTs) const {
  if (hasTimestamp) {
    return parsedTs;
  }

  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}
