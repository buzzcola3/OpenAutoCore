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

#include <Common/MessageType.hpp>
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
#include <nlohmann/json.hpp>

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
    constexpr const char* kUserConfig    = "configuration/UserServiceDiscoveryResponse.json";
    constexpr const char* kDefaultConfig = "configuration/ServiceDiscoveryResponse.default.json";

    const char* path = kUserConfig;
    std::ifstream file(path);
    if (!file.good()) {
        path = kDefaultConfig;
        file.open(path);
    }
    if (!file.good()) {
        AASDK_LOG(error) << "[LiteInputSource] No config found; using "
                         << touchWidth_ << "x" << touchHeight_;
        return;
    }

    try {
        const auto root = nlohmann::json::parse(file);
        for (const auto& ch : root.at("channels")) {
            if (ch.contains("media_sink_service")) {
                const auto& ms = ch["media_sink_service"];
                if (ms.contains("video_configs")) {
                    const auto& vc = ms["video_configs"];
                    const auto& entry = vc.is_array() ? vc.at(0) : vc;
                    marginX_ = entry.value("width_margin", 0u);
                    marginY_ = entry.value("height_margin", 0u);
                }
            }
            if (!ch.contains("input_source_service")) continue;
            const auto& ts = ch["input_source_service"]["touchscreen"];
            const auto& entry = ts.is_array() ? ts.at(0) : ts;
            touchWidth_  = entry.at("width").get<uint32_t>();
            touchHeight_ = entry.at("height").get<uint32_t>();
            AASDK_LOG(debug) << "[LiteInputSource] Touch resolution: "
                             << touchWidth_ << "x" << touchHeight_
                             << " margin=(" << marginX_ << ", " << marginY_ << ")"
                             << " (from " << path << ")";
            return;
        }
        AASDK_LOG(error) << "[LiteInputSource] No input_source_service found in " << path;
    } catch (const std::exception& e) {
        AASDK_LOG(error) << "[LiteInputSource] Failed to parse " << path << ": " << e.what();
    }
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

    const auto toPixel = [](float norm, uint32_t dim, uint32_t margin) -> uint32_t {
        auto screen = static_cast<int64_t>(std::lround(norm * static_cast<float>(dim - 1)));
        auto video  = screen - static_cast<int64_t>(margin / 2);
        auto maxVal = static_cast<int64_t>(dim - margin - 1);
        return static_cast<uint32_t>(std::max<int64_t>(0, std::min(video, maxVal)));
    };

    uint32_t px = toPixel(normX, touchWidth_,  marginX_);
    uint32_t py = toPixel(normY, touchHeight_, marginY_);

    AASDK_LOG(info) << "[LiteInputSource] TOUCH norm=(" << normX << ", " << normY
                    << ") px=(" << px << ", " << py << ") action=" << action
                    << " ptr=" << pointerId;

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
