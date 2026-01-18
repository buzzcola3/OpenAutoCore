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

class MediaSourceMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);

  void onMicrophoneAudio(std::uint64_t timestamp,
                         const void* data,
                         std::size_t size) const;

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;
  bool handleMediaChannelSetupRequest(const ::aasdk::messenger::Message& message,
                                      const std::uint8_t* data,
                                      std::size_t size) const;
  bool handleMediaChannelAck(const std::uint8_t* data,
                             std::size_t size) const;
  bool handleMicrophoneRequest(const ::aasdk::messenger::Message& message,
                               const std::uint8_t* data,
                               std::size_t size) const;

  mutable std::uint64_t messageCount_{0};
  mutable bool channelOpen_{false};
  mutable bool microphoneEnabled_{false};
  mutable std::int32_t sessionId_{0};
  mutable ::aasdk::messenger::ChannelId mediaSourceChannelId_{::aasdk::messenger::ChannelId::NONE};
  mutable ::aasdk::messenger::EncryptionType mediaSourceEncryptionType_{::aasdk::messenger::EncryptionType::PLAIN};
  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}