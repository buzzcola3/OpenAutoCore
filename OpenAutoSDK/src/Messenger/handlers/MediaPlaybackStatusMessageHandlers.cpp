#include <Messenger/handlers/MediaPlaybackStatusMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/mediaplayback/MediaPlaybackStatusMessageId.pb.h>
#include <aap_protobuf/service/mediaplayback/message/MediaPlaybackStatus.pb.h>
#include <aap_protobuf/service/mediaplayback/message/MediaPlaybackMetadata.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using MediaPlayback = aap_protobuf::service::mediaplayback::MediaPlaybackStatusMessageId;
constexpr const char* kLogPrefix = "[MediaPlaybackStatusMessageHandlers]";

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

bool MediaPlaybackStatusMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " media playback status payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case MediaPlayback::MEDIA_PLAYBACK_STATUS:
      return handlePlaybackStatus(payloadData, payloadSize);
    case MediaPlayback::MEDIA_PLAYBACK_METADATA:
      return handlePlaybackMetadata(payloadData, payloadSize);
    case MediaPlayback::MEDIA_PLAYBACK_INPUT:
      return handlePlaybackInput(message, payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void MediaPlaybackStatusMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

bool MediaPlaybackStatusMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                                   const std::uint8_t* data,
                                                                   std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  mediaPlaybackStatusChannelId_ = message.getChannelId();
  mediaPlaybackStatusEncryptionType_ = message.getEncryptionType();

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

bool MediaPlaybackStatusMessageHandlers::handlePlaybackStatus(const std::uint8_t* data,
                                                               std::size_t size) const {
  aap_protobuf::service::mediaplayback::message::MediaPlaybackStatus status;
  if (!parsePayload(data, size, status, "MediaPlaybackStatus")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaPlaybackStatus: " << status.ShortDebugString();
  return true;
}

bool MediaPlaybackStatusMessageHandlers::handlePlaybackMetadata(const std::uint8_t* data,
                                                                 std::size_t size) const {
  aap_protobuf::service::mediaplayback::message::MediaPlaybackMetadata metadata;
  if (!parsePayload(data, size, metadata, "MediaPlaybackMetadata")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " MediaPlaybackMetadata: " << metadata.ShortDebugString();
  return true;
}

bool MediaPlaybackStatusMessageHandlers::handlePlaybackInput(const ::aasdk::messenger::Message& message,
                                                             const std::uint8_t* data,
                                                             std::size_t size) const {
  AASDK_LOG(debug) << kLogPrefix << " MediaPlaybackInput message id="
                   << ::aasdk::messenger::MessageId(message.getPayload()).getId()
                   << " bytes=" << size;
  (void)data;
  return true;
}

}