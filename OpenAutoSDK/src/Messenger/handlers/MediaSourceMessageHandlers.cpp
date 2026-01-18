#include <Messenger/handlers/MediaSourceMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
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
constexpr const char* kLogPrefix = "[MediaSourceMessageHandlers]";

template<typename Proto>
bool parsePayload(const std::uint8_t* data, std::size_t size, Proto& message, const char* label) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    AASDK_LOG(error) << kLogPrefix << " " << label << " payload too large, bytes=" << size;
    return false;
  }

  if (!message.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << kLogPrefix << " Failed to parse " << label << " payload, bytes=" << size;
    return false;
  }

  return true;
}

}

namespace aasdk::messenger::interceptor {

bool MediaSourceMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " media source payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case Media::MEDIA_MESSAGE_SETUP:
      return handleMediaChannelSetupRequest(message, payloadData, payloadSize);
    case Media::MEDIA_MESSAGE_MICROPHONE_REQUEST:
      return handleMicrophoneRequest(message, payloadData, payloadSize);
    case Media::MEDIA_MESSAGE_ACK:
      return handleMediaChannelAck(payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void MediaSourceMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

bool MediaSourceMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                          const std::uint8_t* data,
                                                          std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  mediaSourceChannelId_ = message.getChannelId();
  mediaSourceEncryptionType_ = message.getEncryptionType();
  channelOpen_ = true;

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send channel open response.";
  return false;
}

bool MediaSourceMessageHandlers::handleMediaChannelSetupRequest(const ::aasdk::messenger::Message& message,
                                                                 const std::uint8_t* data,
                                                                 std::size_t size) const {
  aap_protobuf::service::media::shared::message::Setup request;
  if (!parsePayload(data, size, request, "MediaSetup")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaSetup: " << request.ShortDebugString();

  aap_protobuf::service::media::shared::message::Config response;
  response.set_status(aap_protobuf::service::media::shared::message::Config::STATUS_READY);
  response.set_max_unacked(1);
  response.add_configuration_indices(0);

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::SPECIFIC,
                          Media::MEDIA_MESSAGE_SETUP,
                          response);
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send setup response.";
  return false;
}

bool MediaSourceMessageHandlers::handleMicrophoneRequest(const ::aasdk::messenger::Message& message,
                                                         const std::uint8_t* data,
                                                         std::size_t size) const {
  aap_protobuf::service::media::source::message::MicrophoneRequest request;
  if (!parsePayload(data, size, request, "MicrophoneRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MicrophoneRequest: " << request.ShortDebugString();

  microphoneEnabled_ = request.open();
  if (!request.open()) {
    sessionId_ = 0;
  }

  aap_protobuf::service::media::source::message::MicrophoneResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
  response.set_session_id(sessionId_);

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::SPECIFIC,
                          Media::MEDIA_MESSAGE_MICROPHONE_REQUEST,
                          response);
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send microphone response.";
  return false;
}

bool MediaSourceMessageHandlers::handleMediaChannelAck(const std::uint8_t* data,
                                                       std::size_t size) const {
  aap_protobuf::service::media::source::message::Ack indication;
  if (!parsePayload(data, size, indication, "MediaAck")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaAck: " << indication.ShortDebugString();
  return true;
}

void MediaSourceMessageHandlers::onMicrophoneAudio(std::uint64_t timestamp,
                                                   const void* data,
                                                   std::size_t size) const {
  if (sender_ == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send microphone audio.";
    return;
  }

  if (!channelOpen_) {
    AASDK_LOG(error) << kLogPrefix << " Channel not open; dropping microphone audio.";
    return;
  }

  if (!microphoneEnabled_) {
    AASDK_LOG(debug) << kLogPrefix << " Microphone not enabled; dropping audio.";
    return;
  }

  if (data == nullptr || size == 0) {
    AASDK_LOG(error) << kLogPrefix << " Microphone audio payload invalid.";
    return;
  }

  auto timestampData = ::aasdk::messenger::Timestamp(timestamp).getData();
  ::aasdk::common::Data payload;
  payload.reserve(timestampData.size() + size);
  ::aasdk::common::copy(payload, ::aasdk::common::DataConstBuffer(timestampData));
  ::aasdk::common::copy(payload, ::aasdk::common::DataConstBuffer(data, size));

  sender_->sendRaw(mediaSourceChannelId_,
                   mediaSourceEncryptionType_,
                   ::aasdk::messenger::MessageType::SPECIFIC,
                   Media::MEDIA_MESSAGE_DATA,
                   ::aasdk::common::DataConstBuffer(payload));
}

}