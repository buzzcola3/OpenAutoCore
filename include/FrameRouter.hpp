// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
//
// FrameRouter — single-module replacement for the Transport/Messenger stack.
//
// Receives raw bytes from DeviceConnection, parses AA wire frames, reassembles
// multi-frame messages, decrypts, and dispatches to Lite channel handlers.
// Provides a synchronous send path for handlers to frame, encrypt, and
// transmit outbound messages.

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <DeviceManager/DeviceConnection.hpp>
#include <Messenger/ICryptor.hpp>
#include <Messenger/ChannelId.hpp>
#include <Messenger/EncryptionType.hpp>
#include <Messenger/FrameType.hpp>
#include <Messenger/MessageType.hpp>
#include <Lite/FrameIO.hpp>

namespace aasdk {

class FrameRouter {
public:
    using Pointer = std::shared_ptr<FrameRouter>;

    FrameRouter(DeviceConnection::Pointer connection,
                messenger::ICryptor::Pointer cryptor);
    ~FrameRouter();

    /// Start receiving data from the connection.
    void start();

    /// Stop processing and disconnect.
    void stop();

    /// Send a message on the given channel.
    /// Payload includes the 2-byte messageId + protobuf body (same as SendFn).
    /// Fragments if payload > 16KB, encrypts if specified.
    void send(messenger::ChannelId channelId,
              messenger::EncryptionType enc,
              messenger::MessageType msgType,
              const uint8_t* payload, size_t size);

    /// Returns a SendFn compatible with Lite handlers.
    lite::SendFn makeSendFn();

    messenger::ICryptor& cryptor() { return *cryptor_; }

private:
    void onData(const uint8_t* data, size_t len);
    void processBuffer();

    bool sendFrame(messenger::ChannelId ch, messenger::FrameType ft,
                   messenger::EncryptionType enc, messenger::MessageType mt,
                   const uint8_t* payload, size_t size,
                   size_t totalMessageSize);

    DeviceConnection::Pointer connection_;
    messenger::ICryptor::Pointer cryptor_;

    std::vector<uint8_t> inBuffer_;

    struct PartialMessage {
        messenger::EncryptionType encryptionType;
        messenger::MessageType messageType;
        std::vector<uint8_t> payload;
    };
    std::unordered_map<int, PartialMessage> partials_;

    std::mutex sendMutex_;
    bool stopped_ = false;

    static constexpr size_t kMaxFramePayload = 0x4000;
};

} // namespace aasdk
