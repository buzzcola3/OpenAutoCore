#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace aasdk::messenger {
  class Message;
  class MessageSender;
}

namespace aasdk::messenger::interceptor {

class MediaSinkVideoMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleMediaData(const ::aasdk::messenger::Message& message,
                       const std::uint8_t* data,
                       std::size_t size) const;

  bool handleCodecConfig(const ::aasdk::messenger::Message& message,
                         const std::uint8_t* data,
                         std::size_t size) const;

  mutable int32_t sessionId_{-1};
  mutable std::uint64_t messageCount_{0};

  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}
