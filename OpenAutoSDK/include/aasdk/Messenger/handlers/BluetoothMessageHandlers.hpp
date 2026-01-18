#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>

namespace aasdk::messenger {
  class Message;
  class MessageSender;
}

namespace aasdk::messenger::interceptor {

class BluetoothMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);
  void setIsPairedCallback(std::function<bool(const std::string&)> callback);

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;
  bool handleBluetoothPairingRequest(const ::aasdk::messenger::Message& message,
                                     const std::uint8_t* data,
                                     std::size_t size) const;
  bool handleBluetoothAuthenticationResult(const std::uint8_t* data,
                                           std::size_t size) const;

  mutable std::uint64_t messageCount_{0};
  mutable ::aasdk::messenger::ChannelId bluetoothChannelId_{::aasdk::messenger::ChannelId::NONE};
  mutable ::aasdk::messenger::EncryptionType bluetoothEncryptionType_{::aasdk::messenger::EncryptionType::PLAIN};
  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
  std::function<bool(const std::string&)> isPaired_;
};

}