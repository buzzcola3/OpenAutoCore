#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>
#include <Messenger/FrameType.hpp>
#include <Messenger/MessageType.hpp>
#include <Messenger/ICryptor.hpp>
#include <Lite/USBDevice.hpp>

namespace aasdk::lite {

struct InMessage {
    messenger::ChannelId      channelId{messenger::ChannelId::NONE};
    messenger::EncryptionType encryptionType{messenger::EncryptionType::PLAIN};
    messenger::MessageType    messageType{messenger::MessageType::SPECIFIC};
    std::vector<uint8_t>      payload; // complete decrypted payload
};

/// Callback type for sending a raw payload (2-byte messageId + protobuf body).
using SendFn = std::function<void(messenger::ChannelId,
                                  messenger::EncryptionType,
                                  messenger::MessageType,
                                  const uint8_t*, size_t)>;

class FrameIO {
public:
    FrameIO(USBDevice& device, messenger::ICryptor::Pointer cryptor);

    /// Read one complete message (blocking).
    /// Handles multi-frame reassembly and decryption internally.
    /// Returns false on USB error/disconnect.
    bool readMessage(InMessage& out);

    /// Send a message (blocking).
    /// Fragments if payload > kMaxFramePayload, encrypts if specified.
    /// Returns false on USB error.
    bool sendMessage(messenger::ChannelId channelId,
                     messenger::EncryptionType enc,
                     messenger::MessageType msgType,
                     const uint8_t* payload, size_t size);

    static constexpr size_t kMaxFramePayload = 0x4000;

    // ─── Visible for testing ───

    /// Encode a 2-byte frame header.
    static void encodeFrameHeader(uint8_t* out,
                                  messenger::ChannelId ch,
                                  messenger::FrameType ft,
                                  messenger::EncryptionType enc,
                                  messenger::MessageType mt);

    /// Decode a 2-byte frame header.
    static void decodeFrameHeader(const uint8_t* in,
                                  messenger::ChannelId& ch,
                                  messenger::FrameType& ft,
                                  messenger::EncryptionType& enc,
                                  messenger::MessageType& mt);

    /// Encode a frame size field (2 bytes SHORT, 6 bytes EXTENDED).
    /// Returns the number of bytes written (2 or 6).
    static size_t encodeFrameSize(uint8_t* out,
                                  messenger::FrameType ft,
                                  uint16_t framePayloadSize,
                                  uint32_t totalMessageSize);

    /// Decode a frame size field.
    /// `ft` determines whether to read 2 or 6 bytes.
    /// Returns frame payload size; sets totalSize for FIRST frames.
    static uint16_t decodeFrameSize(const uint8_t* in,
                                    messenger::FrameType ft,
                                    uint32_t& totalSize);

private:
    bool readFrame(messenger::ChannelId& ch, messenger::FrameType& ft,
                   messenger::EncryptionType& enc, messenger::MessageType& mt,
                   std::vector<uint8_t>& payload, uint32_t& totalSize);

    bool writeFrame(messenger::ChannelId ch, messenger::FrameType ft,
                    messenger::EncryptionType enc, messenger::MessageType mt,
                    const uint8_t* payload, size_t size,
                    size_t totalMessageSize);

    USBDevice& device_;
    messenger::ICryptor::Pointer cryptor_;

    struct PartialMessage {
        messenger::EncryptionType encryptionType;
        messenger::MessageType    messageType;
        std::vector<uint8_t>      payload;
    };
    std::unordered_map<int, PartialMessage> partials_;
};

} // namespace aasdk::lite
