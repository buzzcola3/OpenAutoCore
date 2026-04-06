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
#include <DeviceManager/Common/DeviceConnection.hpp>
#include <Common/ICryptor.hpp>
#include <Common/ChannelId.hpp>
#include <Common/EncryptionType.hpp>
#include <Common/FrameType.hpp>
#include <Common/MessageType.hpp>
#include <Lite/FrameIO.hpp>
#include <Lite/BluetoothHandler.hpp>
#include <Lite/ControlHandler.hpp>
#include <Lite/GenericNotificationHandler.hpp>
#include <Lite/GuidanceAudioHandler.hpp>
#include <Lite/InputSourceHandler.hpp>
#include <Lite/MediaBrowserHandler.hpp>
#include <Lite/MediaPlaybackStatusHandler.hpp>
#include <Lite/MediaSinkAudioHandler.hpp>
#include <Lite/MediaSinkVideoHandler.hpp>
#include <Lite/MediaSourceHandler.hpp>
#include <Lite/NavigationStatusHandler.hpp>
#include <Lite/PhoneStatusHandler.hpp>
#include <Lite/RadioHandler.hpp>
#include <Lite/SensorHandler.hpp>
#include <Lite/SystemAudioHandler.hpp>
#include <Lite/TelephonyAudioHandler.hpp>
#include <Lite/VendorExtensionHandler.hpp>

namespace buzz { namespace autoapp { namespace Transport { class Transport; } } }

namespace aasdk {

class FrameRouter {
public:
    using Pointer = std::shared_ptr<FrameRouter>;

    FrameRouter(DeviceConnection::Pointer connection,
                messenger::ICryptor::Pointer cryptor,
                std::shared_ptr<buzz::autoapp::Transport::Transport> transport);
    ~FrameRouter();

    /// Start receiving data from the connection.
    void start();

    /// Stop processing and disconnect.
    void stop();

    /// Send a message on the given channel.
    void send(messenger::ChannelId channelId,
              messenger::EncryptionType enc,
              messenger::MessageType msgType,
              const uint8_t* payload, size_t size);

    /// Returns a SendFn compatible with Lite handlers.
    lite::SendFn makeSendFn();

    messenger::ICryptor& cryptor() { return *cryptor_; }

    // ── Handler accessors ──
    lite::ControlHandler& controlHandler() { return controlHandler_; }
    lite::InputSourceHandler& inputSourceHandler() { return inputSourceHandler_; }
    lite::SensorHandler& sensorHandler() { return sensorHandler_; }
    lite::BluetoothHandler& bluetoothHandler() { return bluetoothHandler_; }
    lite::MediaSourceHandler& mediaSourceHandler() { return mediaSourceHandler_; }

private:
    void onData(const uint8_t* data, size_t len);
    void processBuffer();
    void dispatchMessage(const lite::InMessage& msg);

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

    // ── Handlers (owned) ──
    lite::InputSourceHandler inputSourceHandler_;
    lite::SensorHandler sensorHandler_;
    lite::BluetoothHandler bluetoothHandler_;
    lite::MediaSourceHandler mediaSourceHandler_;
    lite::PhoneStatusHandler phoneStatusHandler_;
    lite::GenericNotificationHandler genericNotificationHandler_;
    lite::NavigationStatusHandler navigationStatusHandler_;
    lite::RadioHandler radioHandler_;
    lite::MediaBrowserHandler mediaBrowserHandler_;
    lite::MediaPlaybackStatusHandler mediaPlaybackStatusHandler_;
    lite::VendorExtensionHandler vendorExtensionHandler_;
    lite::ControlHandler controlHandler_;

    // Media sink handlers (need transport)
    lite::MediaSinkVideoHandler mediaSinkVideoHandler_;
    lite::MediaSinkAudioHandler mediaSinkAudioHandler_;
    lite::GuidanceAudioHandler guidanceAudioHandler_;
    lite::SystemAudioHandler systemAudioHandler_;
    lite::TelephonyAudioHandler telephonyAudioHandler_;

    static constexpr size_t kMaxFramePayload = 0x4000;
};

} // namespace aasdk
