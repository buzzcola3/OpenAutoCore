#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>
#include <Messenger/MessageType.hpp>

namespace google::protobuf { class MessageLite; }

namespace aasdk::lite {

struct InMessage;
using SendFn = std::function<void(messenger::ChannelId,
                                  messenger::EncryptionType,
                                  messenger::MessageType,
                                  const uint8_t*, size_t)>;

/// Lite-stack handler for INPUT_SOURCE channel.
class InputSourceHandler {
public:
    explicit InputSourceHandler(SendFn sender);

    /// ChannelRouter-compatible inbound callback.
    void operator()(const InMessage& msg);

    /// Called from transport when a touch event arrives (HU → Phone).
    void onTouchEvent(uint64_t timestamp, const void* data, size_t size);

private:
    void handleChannelOpenRequest(const InMessage& msg,
                                  const uint8_t* data, size_t size);
    void handleKeyBindingRequest(const InMessage& msg,
                                 const uint8_t* data, size_t size);

    void sendProto(messenger::ChannelId ch,
                   messenger::EncryptionType enc,
                   messenger::MessageType mt,
                   uint16_t messageId,
                   const google::protobuf::MessageLite& proto);

    void resolveTouchscreenResolution();

    SendFn send_;
    uint32_t touchWidth_{1920};
    uint32_t touchHeight_{1080};
    messenger::ChannelId touchChannelId_{messenger::ChannelId::NONE};
    messenger::EncryptionType touchEncryptionType_{messenger::EncryptionType::PLAIN};
    uint64_t messageCount_{0};
};

} // namespace aasdk::lite
