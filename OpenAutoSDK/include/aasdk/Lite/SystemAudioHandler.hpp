#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>
#include <Messenger/MessageType.hpp>

namespace google::protobuf { class MessageLite; }

namespace buzz::autoapp::Transport {
  class Transport;
}

namespace aasdk::lite {

struct InMessage;
using SendFn = std::function<void(messenger::ChannelId,
                                  messenger::EncryptionType,
                                  messenger::MessageType,
                                  const uint8_t*, size_t)>;

/// Lite-stack handler for MEDIA_SINK_SYSTEM_AUDIO channel.
class SystemAudioHandler {
public:
    SystemAudioHandler(SendFn sender,
                       std::shared_ptr<buzz::autoapp::Transport::Transport> transport);

    /// ChannelRouter-compatible callback.
    void operator()(const InMessage& msg);

private:
    void handleChannelOpenRequest(const InMessage& msg,
                                  const uint8_t* data, size_t size);
    void handleMediaSetup(const InMessage& msg,
                          const uint8_t* data, size_t size);
    void handleCodecConfig(const InMessage& msg,
                           const uint8_t* data, size_t size);
    void handleMediaData(const InMessage& msg,
                         const uint8_t* data, size_t size);

    void sendProto(messenger::ChannelId ch,
                   messenger::EncryptionType enc,
                   messenger::MessageType mt,
                   uint16_t messageId,
                   const google::protobuf::MessageLite& proto);

    bool ensureTransportStarted();
    uint64_t resolveTimestamp(bool hasTimestamp, uint64_t parsedTs) const;

    SendFn send_;
    std::shared_ptr<buzz::autoapp::Transport::Transport> transport_;
    int32_t sessionId_{-1};
    uint64_t messageCount_{0};
};

} // namespace aasdk::lite
