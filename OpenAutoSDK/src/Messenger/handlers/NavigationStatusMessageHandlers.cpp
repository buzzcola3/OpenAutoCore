#include <Messenger/handlers/NavigationStatusMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
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
constexpr const char* kLogPrefix = "[NavigationStatusMessageHandlers]";

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

bool NavigationStatusMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " navigation status payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case Navigation::INSTRUMENT_CLUSTER_NAVIGATION_STATUS:
      return handleNavigationStatus(payloadData, payloadSize);
    case Navigation::INSTRUMENT_CLUSTER_NAVIGATION_STATE:
      return handleNavigationState(payloadData, payloadSize);
    case Navigation::INSTRUMENT_CLUSTER_NAVIGATION_CURRENT_POSITION:
      return handleNavigationCurrentPosition(payloadData, payloadSize);
    case Navigation::INSTRUMENT_CLUSTER_START:
      return handleNavigationStatusStart(payloadData, payloadSize);
    case Navigation::INSTRUMENT_CLUSTER_STOP:
      return handleNavigationStatusStop(payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void NavigationStatusMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

bool NavigationStatusMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                               const std::uint8_t* data,
                                                               std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  navigationStatusChannelId_ = message.getChannelId();
  navigationStatusEncryptionType_ = message.getEncryptionType();

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

bool NavigationStatusMessageHandlers::handleNavigationStatus(const std::uint8_t* data,
                                                             std::size_t size) const {
  aap_protobuf::service::navigationstatus::message::NavigationStatus status;
  if (!parsePayload(data, size, status, "NavigationStatus")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " NavigationStatus: " << status.ShortDebugString();
  return true;
}

bool NavigationStatusMessageHandlers::handleNavigationState(const std::uint8_t* data,
                                                            std::size_t size) const {
  aap_protobuf::service::navigationstatus::message::NavigationState state;
  if (!parsePayload(data, size, state, "NavigationState")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " NavigationState: " << state.ShortDebugString();
  return true;
}

bool NavigationStatusMessageHandlers::handleNavigationCurrentPosition(const std::uint8_t* data,
                                                                      std::size_t size) const {
  aap_protobuf::service::navigationstatus::message::NavigationCurrentPosition position;
  if (!parsePayload(data, size, position, "NavigationCurrentPosition")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " NavigationCurrentPosition: "
                   << position.ShortDebugString();
  return true;
}

bool NavigationStatusMessageHandlers::handleNavigationStatusStart(const std::uint8_t* data,
                                                                  std::size_t size) const {
  aap_protobuf::service::navigationstatus::message::NavigationStatusStart start;
  if (!parsePayload(data, size, start, "NavigationStatusStart")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " NavigationStatusStart: " << start.ShortDebugString();
  return true;
}

bool NavigationStatusMessageHandlers::handleNavigationStatusStop(const std::uint8_t* data,
                                                                 std::size_t size) const {
  aap_protobuf::service::navigationstatus::message::NavigationStatusStop stop;
  if (!parsePayload(data, size, stop, "NavigationStatusStop")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " NavigationStatusStop: " << stop.ShortDebugString();
  return true;
}

}
