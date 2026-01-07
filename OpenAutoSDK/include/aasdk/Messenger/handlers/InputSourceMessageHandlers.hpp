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

class InputSourceMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);

  void onTouchEvent(std::uint64_t timestamp,
                    const void* data,
                    std::size_t size) const;

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleKeyBindingRequest(const ::aasdk::messenger::Message& message,
                               const std::uint8_t* data,
                               std::size_t size) const;

  void resolveTouchscreenResolution() const;

  mutable std::uint64_t messageCount_{0};
  mutable std::uint32_t touchWidth_{1920};
  mutable std::uint32_t touchHeight_{1080};
  mutable ::aasdk::messenger::ChannelId touchChannelId_{::aasdk::messenger::ChannelId::NONE};
  mutable ::aasdk::messenger::EncryptionType touchEncryptionType_{::aasdk::messenger::EncryptionType::PLAIN};
  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}
