#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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

class NavigationStatusHandler {
public:
    explicit NavigationStatusHandler(SendFn sender);
    void operator()(const InMessage& msg);

private:
    void handleChannelOpenRequest(const InMessage& msg,
                                  const uint8_t* data, size_t size);

    void sendProto(messenger::ChannelId ch,
                   messenger::EncryptionType enc,
                   messenger::MessageType mt,
                   uint16_t messageId,
                   const google::protobuf::MessageLite& proto);

    SendFn send_;
    uint64_t messageCount_{0};
};

} // namespace aasdk::lite
