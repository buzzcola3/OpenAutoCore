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

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <Common/ChannelId.hpp>
#include <Common/EncryptionType.hpp>
#include <Common/MessageType.hpp>

namespace google::protobuf { class MessageLite; }

namespace buzz::autoapp::Transport {
  class Transport;
}

namespace aasdk::lite {

/// Incoming message — same struct as in FrameIO.hpp but forward-declared
/// to avoid pulling the full Lite/FrameIO.hpp dependency.
struct InMessage;

/// Callback type for sending a raw payload (2-byte messageId + protobuf body)
/// over any transport layer — FrameIO (Lite) or MessageSender (old stack).
using SendFn = std::function<void(messenger::ChannelId,
                                  messenger::EncryptionType,
                                  messenger::MessageType,
                                  const uint8_t*, size_t)>;

/// Lite-stack handler for MEDIA_SINK_VIDEO channel.
/// Works with both the Lite ChannelRouter and the old interceptor.
class MediaSinkVideoHandler {
public:
    MediaSinkVideoHandler(SendFn sender,
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
