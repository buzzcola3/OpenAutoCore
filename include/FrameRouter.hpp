// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

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
