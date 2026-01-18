#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>

namespace aasdk::messenger {
  class Message;
  class MessageSender;
}

namespace aasdk::messenger::interceptor {

class NavigationStatusMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleNavigationStatus(const std::uint8_t* data,
                              std::size_t size) const;

  bool handleNavigationState(const std::uint8_t* data,
                             std::size_t size) const;

  bool handleNavigationCurrentPosition(const std::uint8_t* data,
                                       std::size_t size) const;

  bool handleNavigationStatusStart(const std::uint8_t* data,
                                   std::size_t size) const;

  bool handleNavigationStatusStop(const std::uint8_t* data,
                                  std::size_t size) const;

  mutable std::uint64_t messageCount_{0};
  mutable ::aasdk::messenger::ChannelId navigationStatusChannelId_{::aasdk::messenger::ChannelId::NONE};
  mutable ::aasdk::messenger::EncryptionType navigationStatusEncryptionType_{::aasdk::messenger::EncryptionType::PLAIN};
  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}
