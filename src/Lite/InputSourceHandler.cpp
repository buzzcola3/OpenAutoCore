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

#include <Lite/InputSourceHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/inputsource/InputMessageId.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingRequest.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingResponse.pb.h>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Input   = aap_protobuf::service::inputsource::InputMessageId;

} // namespace

namespace aasdk::lite {

InputSourceHandler::InputSourceHandler(SendFn sender)
    : send_(std::move(sender)) {}

void InputSourceHandler::operator()(const InMessage& msg) {
    ++messageCount_;
    const auto& payload = msg.payload;

    if (payload.size() < 2) {
        AASDK_LOG(error) << "[LiteInputSource] payload too small";
        return;
    }

    uint16_t msgId = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    const uint8_t* data = payload.data() + 2;
    size_t size = payload.size() - 2;

    switch (msgId) {
        case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
            handleChannelOpenRequest(msg, data, size);
            break;
        case Input::INPUT_MESSAGE_KEY_BINDING_REQUEST:
            handleKeyBindingRequest(msg, data, size);
            break;
        default:
            AASDK_LOG(debug) << "[LiteInputSource] unhandled msgId=" << msgId;
            break;
    }
}

void InputSourceHandler::resolveTouchscreenResolution() {
    constexpr const char* kConfigPath = "configuration/ServiceDiscoveryResponse.textproto";

    uint32_t width = 0;
    uint32_t height = 0;

    const auto trim = [](std::string value) {
        const auto first = value.find_first_not_of(" \t");
        if (first == std::string::npos) return std::string{};
        const auto last = value.find_last_not_of(" \t");
        return value.substr(first, last - first + 1);
    };

    if (std::ifstream file(kConfigPath); file.good()) {
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("codec_resolution") == std::string::npos) continue;
            const auto colonPos = line.find(':');
            if (colonPos == std::string::npos) continue;

            const auto token = trim(line.substr(colonPos + 1));

            static const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> kResolutionLookup = {
                {"VIDEO_800x480",   {800, 480}},
                {"VIDEO_1280x720",  {1280, 720}},
                {"VIDEO_1920x1080", {1920, 1080}},
                {"VIDEO_2560x1440", {2560, 1440}},
                {"VIDEO_3840x2160", {3840, 2160}},
                {"VIDEO_720x1280",  {720, 1280}},
                {"VIDEO_1080x1920", {1080, 1920}},
                {"VIDEO_1440x2560", {1440, 2560}},
                {"VIDEO_2160x3840", {2160, 3840}},
            };

            const auto it = kResolutionLookup.find(token);
            if (it != kResolutionLookup.end()) {
                width = it->second.first;
                height = it->second.second;
                break;
            }

            AASDK_LOG(error) << "[LiteInputSource] Unknown codec_resolution '" << token << "'";
            break;
        }
    }

    if (width == 0 || height == 0) {
        AASDK_LOG(error) << "[LiteInputSource] Failed to resolve resolution; using "
                         << touchWidth_ << "x" << touchHeight_;
        return;
    }

    touchWidth_ = width;
    touchHeight_ = height;
    AASDK_LOG(debug) << "[LiteInputSource] Touch resolution: " << touchWidth_ << "x" << touchHeight_;
}

void InputSourceHandler::handleChannelOpenRequest(const InMessage& msg,
                                                  const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ChannelOpenRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteInputSource] Failed to parse ChannelOpenRequest";
        return;
    }
    AASDK_LOG(debug) << "[LiteInputSource] ChannelOpenRequest: " << request.ShortDebugString();

    touchChannelId_ = msg.channelId;
    touchEncryptionType_ = msg.encryptionType;
    resolveTouchscreenResolution();

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::CONTROL,
              Control::MESSAGE_CHANNEL_OPEN_RESPONSE, response);
}

void InputSourceHandler::handleKeyBindingRequest(const InMessage& msg,
                                                 const uint8_t* data, size_t size) {
    aap_protobuf::service::media::sink::message::KeyBindingRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteInputSource] Failed to parse KeyBindingRequest";
        return;
    }
    AASDK_LOG(debug) << "[LiteInputSource] KeyBindingRequest: " << request.ShortDebugString();

    aap_protobuf::service::media::sink::message::KeyBindingResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Input::INPUT_MESSAGE_KEY_BINDING_RESPONSE, response);
}

void InputSourceHandler::onTouchEvent(uint64_t timestamp,
                                      const void* data, size_t size) {
    constexpr size_t kExpectedSize = sizeof(float) * 2 + sizeof(uint32_t) * 2; // 16 bytes

    if (data == nullptr || size != kExpectedSize) {
        AASDK_LOG(error) << "[LiteInputSource] TOUCH payload invalid, size=" << size;
        return;
    }

    float x = 0.0f, y = 0.0f;
    uint32_t pointerId = 0, action = 0;

    const auto* bytes = static_cast<const uint8_t*>(data);
    std::memcpy(&x, bytes,                              sizeof(float));
    std::memcpy(&y, bytes + sizeof(float),              sizeof(float));
    std::memcpy(&pointerId, bytes + sizeof(float) * 2,  sizeof(uint32_t));
    std::memcpy(&action,    bytes + sizeof(float) * 2 + sizeof(uint32_t), sizeof(uint32_t));

    const auto clamp01 = [](float v) -> float {
        if (std::isnan(v)) return 0.0f;
        return std::max(0.0f, std::min(1.0f, v));
    };

    const float normX = clamp01(x);
    const float normY = clamp01(y);

    const auto toPixel = [](float norm, uint32_t dim) -> uint32_t {
        auto scaled = static_cast<int64_t>(std::lround(norm * static_cast<float>(dim - 1)));
        return static_cast<uint32_t>(std::max<int64_t>(0, std::min<int64_t>(scaled, dim - 1)));
    };

    uint32_t px = toPixel(normX, touchWidth_);
    uint32_t py = toPixel(normY, touchHeight_);

    aap_protobuf::service::inputsource::message::InputReport inputReport;
    inputReport.set_timestamp(timestamp);

    auto* touchEvent = inputReport.mutable_touch_event();
    touchEvent->set_action(static_cast<aap_protobuf::service::inputsource::message::PointerAction>(action));
    auto* touchLocation = touchEvent->add_pointer_data();
    touchLocation->set_x(px);
    touchLocation->set_y(py);
    touchLocation->set_pointer_id(pointerId);

    if (touchChannelId_ != messenger::ChannelId::NONE) {
        sendProto(touchChannelId_, touchEncryptionType_,
                  messenger::MessageType::SPECIFIC,
                  Input::INPUT_MESSAGE_INPUT_REPORT, inputReport);
    } else {
        AASDK_LOG(error) << "[LiteInputSource] Cannot send touch: channel not yet open";
    }
}

void InputSourceHandler::sendProto(messenger::ChannelId ch,
                                   messenger::EncryptionType enc,
                                   messenger::MessageType mt,
                                   uint16_t messageId,
                                   const google::protobuf::MessageLite& proto) {
    size_t protoSize = proto.ByteSizeLong();
    std::vector<uint8_t> buf(2 + protoSize);
    buf[0] = static_cast<uint8_t>(messageId >> 8);
    buf[1] = static_cast<uint8_t>(messageId & 0xFF);
    proto.SerializeToArray(buf.data() + 2, static_cast<int>(protoSize));
    send_(ch, enc, mt, buf.data(), buf.size());
}

} // namespace aasdk::lite
