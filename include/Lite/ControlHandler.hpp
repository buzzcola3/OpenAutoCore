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

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <Common/ChannelId.hpp>
#include <Common/EncryptionType.hpp>
#include <Common/MessageType.hpp>
#include <Common/Data.hpp>

namespace google::protobuf { class MessageLite; }
namespace aasdk::messenger { class ICryptor; }
namespace f1x::openauto::autoapp::configuration { class ServiceConfig; }

namespace aasdk::lite {

struct InMessage;
using SendFn = std::function<void(messenger::ChannelId,
                                  messenger::EncryptionType,
                                  messenger::MessageType,
                                  const uint8_t*, size_t)>;

class ControlHandler {
public:
    explicit ControlHandler(SendFn sender);

    /// Dispatch an inbound control-channel message.
    void operator()(const InMessage& msg);

    // ── Session lifecycle ──

    /// Wire the AA session state machine and start infrastructure.
    void initSession(messenger::ICryptor& cryptor,
                     f1x::openauto::autoapp::configuration::ServiceConfig& serviceConfig,
                     std::function<void()> onSessionEnd);

    /// Tear down session: stop ping thread, clear references.
    void teardownSession();

    /// Send the initial version request (kicks off the AA protocol).
    void sendVersionRequest();

private:
    void sendRaw(uint16_t messageId, messenger::EncryptionType enc,
                 const uint8_t* data, size_t size);
    void sendProto(uint16_t messageId, messenger::EncryptionType enc,
                   const google::protobuf::MessageLite& proto);

    // ── Inbound handlers ──
    void handleVersionResponse(const uint8_t* data, size_t size);
    void handleHandshake(const uint8_t* data, size_t size);
    void handleServiceDiscovery(const uint8_t* data, size_t size);
    void handleAudioFocus(const uint8_t* data, size_t size);
    void handleNavFocus(const uint8_t* data, size_t size);
    void handleByeByeRequest(const uint8_t* data, size_t size);
    void handleByeByeResponse();
    void handlePingResponse(const uint8_t* data, size_t size);

    // ── Ping ──
    void sendPing();
    void schedulePing();
    void pingThreadFunc();

    void triggerSessionEnd();

    SendFn send_;

    // Session state (valid between initSession / teardownSession)
    messenger::ICryptor* cryptor_ = nullptr;
    f1x::openauto::autoapp::configuration::ServiceConfig* serviceConfig_ = nullptr;
    std::function<void()> onSessionEnd_;
    std::thread pingThread_;
    std::mutex pingMutex_;
    std::condition_variable pingCv_;
    std::atomic<int64_t> pingsCount_{0};
    std::atomic<int64_t> pongsCount_{0};
    std::atomic<bool> sessionActive_{false};

    static constexpr int64_t kPingIntervalMs = 5000;
    static constexpr int64_t kMaxMissedPongs = 4;
};

} // namespace aasdk::lite
