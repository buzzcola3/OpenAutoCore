#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <chrono>

namespace buzz::autoapp::Transport {
  class Transport;
}

namespace aasdk::messenger {
  class Message;
  class MessageSender;
}

namespace aasdk::messenger::interceptor {

class GuidanceAudioMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);
  void setTransport(std::shared_ptr<buzz::autoapp::Transport::Transport> transport);

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleMediaData(const ::aasdk::messenger::Message& message,
                       const std::uint8_t* data,
                       std::size_t size) const;

  bool handleChannelSetupRequest(const ::aasdk::messenger::Message& message,
                                 const std::uint8_t* data,
                                 std::size_t size) const;

  bool handleCodecConfig(const ::aasdk::messenger::Message& message,
                         const std::uint8_t* data,
                         std::size_t size) const;

  bool ensureTransportStarted() const;
  uint64_t resolveTimestamp(bool hasTimestamp, uint64_t parsedTs) const;

  mutable int32_t sessionId_{-1};
  mutable std::uint64_t messageCount_{0};
  mutable std::shared_ptr<buzz::autoapp::Transport::Transport> transport_;

  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}
