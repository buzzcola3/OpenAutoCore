#include <Messenger/handlers/MediaSinkAudioMessageHandlers.hpp>

#include <Messenger/Message.hpp>
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
#include <iomanip>
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

template<typename Proto>
void decodeAndLogPayload(const std::uint8_t* data, std::size_t size, const char* label) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] " << label
                     << " payload too large for ParseFromArray, bytes=" << size;
    return;
  }

  Proto message;
  if (!message.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] Failed to parse " << label
                     << " payload, bytes=" << size;
    return;
  }

  AASDK_LOG(debug) << "[MediaSinkAudioMessageHandlers] " << label << ": "
                   << message.ShortDebugString();
}

}

namespace aasdk::messenger::interceptor {

bool MediaSinkAudioMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] media audio payload too small";
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
        AASDK_LOG(debug) << "[MediaSinkAudioMessageHandlers] MediaStart: session=" << sessionId_;
      } else {
        AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] Failed to parse MediaStart payload";
      }
      break;
    }
    case Media::MEDIA_MESSAGE_STOP:
      decodeAndLogPayload<aap_protobuf::service::media::shared::message::Stop>(
          payloadData, payloadSize, "MediaStop");
      break;
    case Media::MEDIA_MESSAGE_CODEC_CONFIG:
      handled = handleCodecConfig(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_DATA:
      handled = handleMediaData(message, payloadData, payloadSize);
      break;
    case Media::MEDIA_MESSAGE_AUDIO_UNDERFLOW_NOTIFICATION:
      AASDK_LOG(warning) << "[MediaSinkAudioMessageHandlers] Audio underflow notification received.";
      handled = true;
      break;
    default:
      AASDK_LOG(debug) << "[MediaSinkAudioMessageHandlers] media audio message id="
                       << messageId.getId() << " not explicitly decoded.";
      break;
  }

  return handled;
}

bool MediaSinkAudioMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                             const std::uint8_t* data,
                                                             std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!request.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] Failed to parse ChannelOpenRequest payload";
    return false;
  }

  AASDK_LOG(debug) << "[MediaSinkAudioMessageHandlers] ChannelOpenRequest: "
                   << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  AASDK_LOG(debug) << "[MediaSinkAudioMessageHandlers] Constructed ChannelOpenResponse: "
                   << response.ShortDebugString();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] MessageSender not configured; cannot send response.";
  return false;
}

bool MediaSinkAudioMessageHandlers::handleMediaData(const ::aasdk::messenger::Message& message,
                                                    const std::uint8_t* data,
                                                    std::size_t size) const {
  if (sender_ == nullptr) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] MessageSender not configured; cannot send media ACK.";
    return false;
  }

  if (sessionId_ < 0) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] Session id not set; cannot send media ACK.";
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
    transport_->send(buzz::wire::MsgType::MEDIA_AUDIO, timestamp, frameData, frameSize);
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

bool MediaSinkAudioMessageHandlers::handleChannelSetupRequest(const ::aasdk::messenger::Message& message,
                                                              const std::uint8_t* data,
                                                              std::size_t size) const {
  aap_protobuf::service::media::shared::message::Setup setup;
  if (!setup.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] Failed to parse MediaSetup payload";
    return false;
  }

  AASDK_LOG(info) << "[MediaSinkAudioMessageHandlers] MediaSetup: channel="
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

    AASDK_LOG(debug) << "[MediaSinkAudioMessageHandlers] MediaSetup response: "
                     << response.ShortDebugString();
    return true;
  }

  AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] MessageSender not configured; cannot send setup response.";
  return false;
}

bool MediaSinkAudioMessageHandlers::handleCodecConfig(const ::aasdk::messenger::Message& message,
                                                      const std::uint8_t* data,
                                                      std::size_t size) const {
  AASDK_LOG(debug) << "[MediaSinkAudioMessageHandlers] codec configuration blob size=" << size
                   << " bytes on channel " << channelIdToString(message.getChannelId());

  return handleMediaData(message, data, size);
}

void MediaSinkAudioMessageHandlers::setMessageSender(
    std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

void MediaSinkAudioMessageHandlers::setTransport(
    std::shared_ptr<buzz::autoapp::Transport::Transport> transport) {
  transport_ = std::move(transport);
}

bool MediaSinkAudioMessageHandlers::ensureTransportStarted() const {
  if (transport_ && transport_->isRunning()) {
    return true;
  }

  if (!transport_) {
    transport_ = std::make_shared<buzz::autoapp::Transport::Transport>();
  }

  if (!transport_->startAsA(std::chrono::microseconds{1000})) {
    AASDK_LOG(error) << "[MediaSinkAudioMessageHandlers] Failed to start OpenAutoTransport (side A).";
    return false;
  }

  return transport_->isRunning();
}

uint64_t MediaSinkAudioMessageHandlers::resolveTimestamp(bool hasTimestamp, uint64_t parsedTs) const {
  if (hasTimestamp) {
    return parsedTs;
  }

  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}
