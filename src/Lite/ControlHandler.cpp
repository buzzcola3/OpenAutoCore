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

#include <Lite/ControlHandler.hpp>
#include <Lite/FrameIO.hpp>
#include <Version.hpp>
#include <Common/Log.hpp>
#include <Error/Error.hpp>
#include <Common/ICryptor.hpp>
#include <Configuration/ServiceConfig.hpp>
#include <boost/endian/conversion.hpp>
#include <chrono>
#include <cstring>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusRequest.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusNotification.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusRequestType.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusStateType.pb.h>
#include <aap_protobuf/service/control/message/AuthResponse.pb.h>
#include <aap_protobuf/service/control/message/ByeByeRequest.pb.h>
#include <aap_protobuf/service/control/message/ByeByeResponse.pb.h>
#include <aap_protobuf/service/control/message/NavFocusNotification.pb.h>
#include <aap_protobuf/service/control/message/NavFocusRequestNotification.pb.h>
#include <aap_protobuf/service/control/message/NavFocusType.pb.h>
#include <aap_protobuf/service/control/message/PingRequest.pb.h>
#include <aap_protobuf/service/control/message/PingResponse.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryRequest.pb.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>

namespace aasdk::lite {

using CMT = aap_protobuf::service::control::message::ControlMessageType;

ControlHandler::ControlHandler(SendFn sender)
    : send_(std::move(sender)) {}

void ControlHandler::operator()(const InMessage& msg) {
    if (msg.payload.size() < 2) {
        AASDK_LOG(error) << "[ControlHandler] Payload too short";
        return;
    }

    uint16_t msgId = (static_cast<uint16_t>(msg.payload[0]) << 8) | msg.payload[1];
    const uint8_t* data = msg.payload.data() + 2;
    size_t size = msg.payload.size() - 2;

    switch (msgId) {
        case CMT::MESSAGE_VERSION_RESPONSE:
            handleVersionResponse(data, size);
            break;
        case CMT::MESSAGE_ENCAPSULATED_SSL:
            handleHandshake(data, size);
            break;
        case CMT::MESSAGE_SERVICE_DISCOVERY_REQUEST:
            handleServiceDiscovery(data, size);
            break;
        case CMT::MESSAGE_AUDIO_FOCUS_REQUEST:
            handleAudioFocus(data, size);
            break;
        case CMT::MESSAGE_NAV_FOCUS_REQUEST:
            handleNavFocus(data, size);
            break;
        case CMT::MESSAGE_BYEBYE_REQUEST:
            handleByeByeRequest(data, size);
            break;
        case CMT::MESSAGE_BYEBYE_RESPONSE:
            handleByeByeResponse();
            break;
        case CMT::MESSAGE_PING_RESPONSE:
            handlePingResponse(data, size);
            break;
        case CMT::MESSAGE_BATTERY_STATUS_NOTIFICATION:
            AASDK_LOG(info) << "[ControlHandler] Battery status notification";
            break;
        case CMT::MESSAGE_VOICE_SESSION_NOTIFICATION:
            AASDK_LOG(info) << "[ControlHandler] Voice session notification";
            break;
        case CMT::MESSAGE_PING_REQUEST:
            AASDK_LOG(debug) << "[ControlHandler] Ping request (from phone)";
            break;
        case CMT::MESSAGE_CHANNEL_OPEN_REQUEST:
            AASDK_LOG(debug) << "[ControlHandler] Channel open request";
            break;
        default:
            AASDK_LOG(warning) << "[ControlHandler] Unhandled message id: " << msgId;
            break;
    }
}

// ── Session lifecycle ──

void ControlHandler::initSession(messenger::ICryptor& cryptor,
                                  f1x::openauto::autoapp::configuration::ServiceConfig& serviceConfig,
                                  boost::asio::io_service& ioService,
                                  std::function<void()> onSessionEnd) {
    cryptor_ = &cryptor;
    serviceConfig_ = &serviceConfig;
    onSessionEnd_ = std::move(onSessionEnd);
    pingsCount_ = 0;
    pongsCount_ = 0;
    pingTimer_ = std::make_unique<boost::asio::deadline_timer>(ioService);
    sessionActive_ = true;
}

void ControlHandler::teardownSession() {
    sessionActive_ = false;
    if (pingTimer_) {
        pingTimer_->cancel();
        pingTimer_.reset();
    }
    cryptor_ = nullptr;
    serviceConfig_ = nullptr;
    onSessionEnd_ = nullptr;
}

// ── Inbound handlers ──

void ControlHandler::handleVersionResponse(const uint8_t* data, size_t size) {
    if (size < 6) {
        AASDK_LOG(error) << "[ControlHandler] Version response too short";
        return;
    }

    const uint16_t* words = reinterpret_cast<const uint16_t*>(data);
    auto status = static_cast<aap_protobuf::shared::MessageStatus>(
        boost::endian::big_to_native(words[2]));

    AASDK_LOG(info) << "[ControlHandler] Version: " << words[0] << "." << words[1]
                    << " status=" << status;

    if (status == aap_protobuf::shared::MessageStatus::STATUS_NO_COMPATIBLE_VERSION) {
        AASDK_LOG(error) << "[ControlHandler] Version mismatch.";
        triggerSessionEnd();
        return;
    }

    if (!cryptor_) return;

    try {
        AASDK_LOG(info) << "[ControlHandler] Beginning SSL handshake.";
        cryptor_->doHandshake();
        auto hsData = cryptor_->readHandshakeBuffer();
        sendRaw(CMT::MESSAGE_ENCAPSULATED_SSL, messenger::EncryptionType::PLAIN,
                hsData.data(), hsData.size());
    } catch (const error::Error& e) {
        AASDK_LOG(error) << "[ControlHandler] Handshake error: " << e.what();
        triggerSessionEnd();
    }
}

void ControlHandler::handleHandshake(const uint8_t* data, size_t size) {
    AASDK_LOG(info) << "[ControlHandler] SSL handshake data, size=" << size;
    if (!cryptor_) return;

    try {
        cryptor_->writeHandshakeBuffer(common::DataConstBuffer(data, size));
        if (!cryptor_->doHandshake()) {
            AASDK_LOG(info) << "[ControlHandler] Continue handshake.";
            auto hsData = cryptor_->readHandshakeBuffer();
            sendRaw(CMT::MESSAGE_ENCAPSULATED_SSL, messenger::EncryptionType::PLAIN,
                    hsData.data(), hsData.size());
        } else {
            AASDK_LOG(info) << "[ControlHandler] Handshake completed.";
            aap_protobuf::service::control::message::AuthResponse auth;
            auth.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);
            sendProto(CMT::MESSAGE_AUTH_COMPLETE, messenger::EncryptionType::PLAIN, auth);
        }
    } catch (const error::Error& e) {
        AASDK_LOG(error) << "[ControlHandler] Handshake error: " << e.what();
        triggerSessionEnd();
    }
}

void ControlHandler::handleServiceDiscovery(const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ServiceDiscoveryRequest req;
    if (!req.ParseFromArray(data, size)) {
        AASDK_LOG(error) << "[ControlHandler] Failed to parse ServiceDiscoveryRequest";
        return;
    }

    AASDK_LOG(info) << "[ControlHandler] ServiceDiscovery: "
                    << req.label_text() << " / " << req.device_name();

    if (serviceConfig_) {
        sendProto(CMT::MESSAGE_SERVICE_DISCOVERY_RESPONSE,
                  messenger::EncryptionType::ENCRYPTED, serviceConfig_->toProto());
    }

    schedulePing();
}

void ControlHandler::handleAudioFocus(const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::AudioFocusRequest req;
    if (!req.ParseFromArray(data, size)) {
        AASDK_LOG(error) << "[ControlHandler] Failed to parse AudioFocusRequest";
        return;
    }

    AASDK_LOG(info) << "[ControlHandler] AudioFocus: "
                    << AudioFocusRequestType_Name(req.audio_focus_type());

    using AFR = aap_protobuf::service::control::message::AudioFocusRequestType;
    using AFS = aap_protobuf::service::control::message::AudioFocusStateType;

    auto state = (req.audio_focus_type() == AFR::AUDIO_FOCUS_RELEASE)
        ? AFS::AUDIO_FOCUS_STATE_LOSS
        : AFS::AUDIO_FOCUS_STATE_GAIN;

    aap_protobuf::service::control::message::AudioFocusNotification resp;
    resp.set_focus_state(state);
    sendProto(CMT::MESSAGE_AUDIO_FOCUS_NOTIFICATION,
              messenger::EncryptionType::ENCRYPTED, resp);
}

void ControlHandler::handleNavFocus(const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::NavFocusRequestNotification req;
    if (!req.ParseFromArray(data, size)) {
        AASDK_LOG(error) << "[ControlHandler] Failed to parse NavFocusRequest";
        return;
    }

    AASDK_LOG(info) << "[ControlHandler] NavFocus: "
                    << NavFocusType_Name(req.focus_type());

    aap_protobuf::service::control::message::NavFocusNotification resp;
    resp.set_focus_type(
        aap_protobuf::service::control::message::NavFocusType::NAV_FOCUS_PROJECTED);
    sendProto(CMT::MESSAGE_NAV_FOCUS_NOTIFICATION,
              messenger::EncryptionType::ENCRYPTED, resp);
}

void ControlHandler::handleByeByeRequest(const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ByeByeRequest req;
    if (!req.ParseFromArray(data, size)) {
        AASDK_LOG(error) << "[ControlHandler] Failed to parse ByeByeRequest";
        return;
    }

    AASDK_LOG(info) << "[ControlHandler] ByeByeRequest reason=" << req.reason();

    aap_protobuf::service::control::message::ByeByeResponse resp;
    sendProto(CMT::MESSAGE_BYEBYE_RESPONSE,
              messenger::EncryptionType::ENCRYPTED, resp);
    triggerSessionEnd();
}

void ControlHandler::handleByeByeResponse() {
    AASDK_LOG(info) << "[ControlHandler] ByeByeResponse";
    triggerSessionEnd();
}

void ControlHandler::handlePingResponse(const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::PingResponse resp;
    if (resp.ParseFromArray(data, size)) {
        AASDK_LOG(debug) << "[ControlHandler] PingResponse ts=" << resp.timestamp();
    }
    pongsCount_.fetch_add(1, std::memory_order_relaxed);
}

// ── Ping ──

void ControlHandler::sendPing() {
    aap_protobuf::service::control::message::PingRequest req;
    auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch());
    req.set_timestamp(ts.count());
    sendProto(CMT::MESSAGE_PING_REQUEST, messenger::EncryptionType::PLAIN, req);
}

void ControlHandler::schedulePing() {
    if (!pingTimer_ || !sessionActive_) return;
    pingsCount_.fetch_add(1, std::memory_order_relaxed);
    pingTimer_->expires_from_now(boost::posix_time::milliseconds(kPingIntervalMs));
    pingTimer_->async_wait([this](const boost::system::error_code& ec) {
        onPingTimer(ec);
    });
}

void ControlHandler::onPingTimer(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted || !sessionActive_) return;

    int64_t missed = pingsCount_.load(std::memory_order_relaxed)
                   - pongsCount_.load(std::memory_order_relaxed);
    if (missed > kMaxMissedPongs) {
        AASDK_LOG(error) << "[ControlHandler] Ping timeout (" << missed << " missed)";
        triggerSessionEnd();
        return;
    }

    sendPing();
    schedulePing();
}

void ControlHandler::triggerSessionEnd() {
    bool expected = true;
    if (!sessionActive_.compare_exchange_strong(expected, false)) return;
    AASDK_LOG(info) << "[ControlHandler] Session ending.";
    if (onSessionEnd_) onSessionEnd_();
}

// ── Wire format ──

void ControlHandler::sendRaw(uint16_t messageId, messenger::EncryptionType enc,
                              const uint8_t* data, size_t size) {
    std::vector<uint8_t> buf(2 + size);
    buf[0] = static_cast<uint8_t>(messageId >> 8);
    buf[1] = static_cast<uint8_t>(messageId & 0xFF);
    if (size > 0) std::memcpy(buf.data() + 2, data, size);
    send_(messenger::ChannelId::CONTROL, enc,
          messenger::MessageType::SPECIFIC, buf.data(), buf.size());
}

void ControlHandler::sendProto(uint16_t messageId, messenger::EncryptionType enc,
                                const google::protobuf::MessageLite& proto) {
    std::string serialized = proto.SerializeAsString();
    sendRaw(messageId, enc,
            reinterpret_cast<const uint8_t*>(serialized.data()), serialized.size());
}

void ControlHandler::sendVersionRequest() {
    AASDK_LOG(debug) << "[ControlHandler] sendVersionRequest()";
    common::Data buf(4, 0);
    reinterpret_cast<uint16_t&>(buf[0]) = boost::endian::native_to_big(AASDK_MAJOR);
    reinterpret_cast<uint16_t&>(buf[2]) = boost::endian::native_to_big(AASDK_MINOR);
    sendRaw(CMT::MESSAGE_VERSION_REQUEST, messenger::EncryptionType::PLAIN,
            buf.data(), buf.size());
}

} // namespace aasdk::lite
