#include <Messenger/handlers/BluetoothMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/bluetooth/BluetoothMessageId.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingRequest.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingResponse.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothAuthenticationData.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothAuthenticationResult.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingMethod.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <limits>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Bluetooth = aap_protobuf::service::bluetooth::BluetoothMessageId;
constexpr const char* kLogPrefix = "[BluetoothMessageHandlers]";

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

bool BluetoothMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " bluetooth payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case Bluetooth::BLUETOOTH_MESSAGE_PAIRING_REQUEST:
      return handleBluetoothPairingRequest(message, payloadData, payloadSize);
    case Bluetooth::BLUETOOTH_MESSAGE_AUTHENTICATION_RESULT:
      return handleBluetoothAuthenticationResult(payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId()
                       << " not explicitly handled.";
      return false;
  }
}

void BluetoothMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

void BluetoothMessageHandlers::setIsPairedCallback(std::function<bool(const std::string&)> callback) {
  isPaired_ = std::move(callback);
}

bool BluetoothMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                        const std::uint8_t* data,
                                                        std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  bluetoothChannelId_ = message.getChannelId();
  bluetoothEncryptionType_ = message.getEncryptionType();

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

bool BluetoothMessageHandlers::handleBluetoothPairingRequest(const ::aasdk::messenger::Message& message,
                                                             const std::uint8_t* data,
                                                             std::size_t size) const {
  aap_protobuf::service::bluetooth::message::BluetoothPairingRequest request;
  if (!parsePayload(data, size, request, "BluetoothPairingRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " BluetoothPairingRequest: " << request.ShortDebugString();

  bool alreadyPaired = false;
  if (isPaired_) {
    alreadyPaired = isPaired_(request.phone_address());
  }

  aap_protobuf::service::bluetooth::message::BluetoothPairingResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
  response.set_already_paired(alreadyPaired);

  if (sender_ == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send pairing response.";
    return false;
  }

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Bluetooth::BLUETOOTH_MESSAGE_PAIRING_RESPONSE,
                        response);

  aap_protobuf::service::bluetooth::message::BluetoothAuthenticationData authData;
  authData.set_auth_data("123456");
  authData.set_pairing_method(request.pairing_method());

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Bluetooth::BLUETOOTH_MESSAGE_AUTHENTICATION_DATA,
                        authData);

  return true;
}

bool BluetoothMessageHandlers::handleBluetoothAuthenticationResult(const std::uint8_t* data,
                                                                    std::size_t size) const {
  aap_protobuf::service::bluetooth::message::BluetoothAuthenticationResult result;
  if (!parsePayload(data, size, result, "BluetoothAuthenticationResult")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " BluetoothAuthenticationResult: " << result.ShortDebugString();
  return true;
}

}