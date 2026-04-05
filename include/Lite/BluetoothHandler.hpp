#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
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

class BluetoothHandler {
public:
    explicit BluetoothHandler(SendFn sender);
    void operator()(const InMessage& msg);

    void setIsPairedCallback(std::function<bool(const std::string&)> callback);

private:
    void handleChannelOpenRequest(const InMessage& msg,
                                  const uint8_t* data, size_t size);
    void handleBluetoothPairingRequest(const InMessage& msg,
                                      const uint8_t* data, size_t size);
    void handleBluetoothAuthenticationResult(const uint8_t* data, size_t size);

    void sendProto(messenger::ChannelId ch,
                   messenger::EncryptionType enc,
                   messenger::MessageType mt,
                   uint16_t messageId,
                   const google::protobuf::MessageLite& proto);

    SendFn send_;
    std::function<bool(const std::string&)> isPaired_;
    uint64_t messageCount_{0};
};

} // namespace aasdk::lite
