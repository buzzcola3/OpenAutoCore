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

class MediaBrowserMessageHandlers {
public:
  bool handle(const ::aasdk::messenger::Message& message) const;
  void setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender);

private:
  bool handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                const std::uint8_t* data,
                                std::size_t size) const;

  bool handleMediaRootNode(const std::uint8_t* data,
                           std::size_t size) const;
  bool handleMediaSourceNode(const std::uint8_t* data,
                             std::size_t size) const;
  bool handleMediaListNode(const std::uint8_t* data,
                           std::size_t size) const;
  bool handleMediaSongNode(const std::uint8_t* data,
                           std::size_t size) const;
  bool handleMediaGetNode(const std::uint8_t* data,
                          std::size_t size) const;
  bool handleMediaBrowseInput(const std::uint8_t* data,
                              std::size_t size) const;

  mutable std::uint64_t messageCount_{0};
  mutable ::aasdk::messenger::ChannelId mediaBrowserChannelId_{::aasdk::messenger::ChannelId::NONE};
  mutable ::aasdk::messenger::EncryptionType mediaBrowserEncryptionType_{::aasdk::messenger::EncryptionType::PLAIN};
  std::shared_ptr<::aasdk::messenger::MessageSender> sender_;
};

}
