#include <Messenger/handlers/GenericNotificationMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/genericnotification/GenericNotificationMessageId.pb.h>
#include <aap_protobuf/service/genericnotification/message/GenericNotificationAck.pb.h>
#include <aap_protobuf/service/genericnotification/message/GenericNotificationMessage.pb.h>
#include <aap_protobuf/service/genericnotification/message/GenericNotificationSubscribe.pb.h>
#include <aap_protobuf/service/genericnotification/message/GenericNotificationUnsubscribe.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using GenericNotification = aap_protobuf::service::genericnotification::GenericNotificationMessageId;
constexpr const char* kLogPrefix = "[GenericNotificationMessageHandlers]";

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

bool GenericNotificationMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " generic notification payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case GenericNotification::GENERIC_NOTIFICATION_SUBSCRIBE:
      return handleNotificationSubscribe(payloadData, payloadSize);
    case GenericNotification::GENERIC_NOTIFICATION_UNSUBSCRIBE:
      return handleNotificationUnsubscribe(payloadData, payloadSize);
    case GenericNotification::GENERIC_NOTIFICATION_MESSAGE:
      return handleNotificationMessage(payloadData, payloadSize);
    case GenericNotification::GENERIC_NOTIFICATION_ACK:
      return handleNotificationAck(payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void GenericNotificationMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

bool GenericNotificationMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                                  const std::uint8_t* data,
                                                                  std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  genericNotificationChannelId_ = message.getChannelId();
  genericNotificationEncryptionType_ = message.getEncryptionType();

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

bool GenericNotificationMessageHandlers::handleNotificationSubscribe(const std::uint8_t* data,
                                                                     std::size_t size) const {
  aap_protobuf::service::genericnotification::message::GenericNotificationSubscribe subscribe;
  if (!parsePayload(data, size, subscribe, "GenericNotificationSubscribe")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " Subscribe: " << subscribe.ShortDebugString();
  return true;
}

bool GenericNotificationMessageHandlers::handleNotificationUnsubscribe(const std::uint8_t* data,
                                                                       std::size_t size) const {
  aap_protobuf::service::genericnotification::message::GenericNotificationUnsubscribe unsubscribe;
  if (!parsePayload(data, size, unsubscribe, "GenericNotificationUnsubscribe")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " Unsubscribe: " << unsubscribe.ShortDebugString();
  return true;
}

bool GenericNotificationMessageHandlers::handleNotificationMessage(const std::uint8_t* data,
                                                                   std::size_t size) const {
  aap_protobuf::service::genericnotification::message::GenericNotificationMessage notification;
  if (!parsePayload(data, size, notification, "GenericNotificationMessage")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " Message: " << notification.ShortDebugString();
  return true;
}

bool GenericNotificationMessageHandlers::handleNotificationAck(const std::uint8_t* data,
                                                               std::size_t size) const {
  aap_protobuf::service::genericnotification::message::GenericNotificationAck ack;
  if (!parsePayload(data, size, ack, "GenericNotificationAck")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " Ack: " << ack.ShortDebugString();
  return true;
}

}
