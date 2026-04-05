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

#include <Lite/SensorHandler.hpp>
#include <Lite/FrameIO.hpp>

#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
#include <nlohmann/json.hpp>
#include <aap_protobuf/service/control/ControlMessageType.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenRequest.pb.h>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/sensorsource/SensorMessageId.pb.h>
#include <aap_protobuf/service/sensorsource/message/DrivingStatus.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorBatch.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorRequest.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorResponse.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorStartResponseMessage.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Sensor  = aap_protobuf::service::sensorsource::SensorMessageId;

} // namespace

namespace aasdk::lite {

SensorHandler::SensorHandler(SendFn sender)
    : send_(std::move(sender)) {}

void SensorHandler::operator()(const InMessage& msg) {
    ++messageCount_;
    const auto& payload = msg.payload;

    if (payload.size() < 2) {
        AASDK_LOG(error) << "[LiteSensor] payload too small";
        return;
    }

    uint16_t msgId = (static_cast<uint16_t>(payload[0]) << 8) | payload[1];
    const uint8_t* data = payload.data() + 2;
    size_t size = payload.size() - 2;

    switch (msgId) {
        case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
            handleChannelOpenRequest(msg, data, size);
            break;
        case Sensor::SENSOR_MESSAGE_REQUEST:
            handleSensorStartRequest(msg, data, size);
            break;
        case Sensor::SENSOR_MESSAGE_RESPONSE:
            handleSensorStopRequest(msg, data, size);
            break;
        default:
            AASDK_LOG(debug) << "[LiteSensor] unhandled msgId=" << msgId;
            break;
    }
}

void SensorHandler::handleChannelOpenRequest(const InMessage& msg,
                                             const uint8_t* data, size_t size) {
    aap_protobuf::service::control::message::ChannelOpenRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteSensor] Failed to parse ChannelOpenRequest";
        return;
    }
    AASDK_LOG(debug) << "[LiteSensor] ChannelOpenRequest: " << request.ShortDebugString();

    sensorChannelId_ = msg.channelId;
    sensorEncryptionType_ = msg.encryptionType;

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::CONTROL,
              Control::MESSAGE_CHANNEL_OPEN_RESPONSE, response);
}

void SensorHandler::handleSensorStartRequest(const InMessage& msg,
                                             const uint8_t* data, size_t size) {
    aap_protobuf::service::sensorsource::message::SensorRequest request;
    if (!request.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteSensor] Failed to parse SensorRequest";
        return;
    }
    AASDK_LOG(debug) << "[LiteSensor] SensorRequest: " << request.ShortDebugString();

    sensorChannelId_ = msg.channelId;
    sensorEncryptionType_ = msg.encryptionType;

    aap_protobuf::service::sensorsource::message::SensorStartResponseMessage response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    sendProto(msg.channelId, msg.encryptionType,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_RESPONSE, response);

    AASDK_LOG(debug) << "[LiteSensor] Sensor start acknowledged; awaiting transport JSON.";
}

void SensorHandler::handleSensorStopRequest(const InMessage& msg,
                                            const uint8_t* data, size_t size) {
    aap_protobuf::service::sensorsource::message::SensorResponse response;
    if (!response.ParseFromArray(data, static_cast<int>(size))) {
        AASDK_LOG(error) << "[LiteSensor] Failed to parse SensorResponse";
        return;
    }
    AASDK_LOG(debug) << "[LiteSensor] SensorResponse (stop/ack): " << response.ShortDebugString();
}

void SensorHandler::onSensorEvent(uint64_t timestamp,
                                  const void* data, size_t size) {
    if (data == nullptr) {
        AASDK_LOG(error) << "[LiteSensor] SENSOR payload nullptr";
        return;
    }

    const auto json = nlohmann::json::parse(
        std::string_view(static_cast<const char*>(data), size), nullptr, false);
    if (json.is_discarded()) {
        AASDK_LOG(error) << "[LiteSensor] Failed to parse SENSOR json ts=" << timestamp
                         << " bytes=" << size;
        return;
    }

    bool handled = false;

    if (json.contains("location") && json["location"].is_object()) {
        sendLocationIndication(json["location"]);
        handled = true;
    } else if (json.contains("location")) {
        AASDK_LOG(error) << "[LiteSensor] json location is not an object";
        return;
    }

    if (json.contains("night_mode") && json["night_mode"].is_object()) {
        sendNightModeIndication(json["night_mode"]);
        handled = true;
    } else if (json.contains("night_mode")) {
        AASDK_LOG(error) << "[LiteSensor] json night_mode is not an object";
        return;
    }

    if (json.contains("driving_status") && json["driving_status"].is_object()) {
        sendDrivingStatusIndication(json["driving_status"]);
        handled = true;
    } else if (json.contains("driving_status")) {
        AASDK_LOG(error) << "[LiteSensor] json driving_status is not an object";
        return;
    }

    if (!handled) {
        AASDK_LOG(debug) << "[LiteSensor] json contained no handled keys ts=" << timestamp;
    }
}

void SensorHandler::sendDrivingStatusIndication(const nlohmann::json& drivingStatus) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) {
        AASDK_LOG(error) << "[LiteSensor] Cannot send driving status: channel not open";
        return;
    }

    if (!drivingStatus.contains("status") || !drivingStatus["status"].is_string()) {
        AASDK_LOG(error) << "[LiteSensor] Driving status json missing status string";
        return;
    }

    const auto status = drivingStatus["status"].get<std::string>();
    auto mapped = aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED;

    if (status == "no_video") {
        mapped = aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_VIDEO;
    } else if (status == "no_keyboard_input") {
        mapped = aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_KEYBOARD_INPUT;
    } else if (status == "no_voice_input") {
        mapped = aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_VOICE_INPUT;
    } else if (status == "no_config") {
        mapped = aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_NO_CONFIG;
    } else if (status == "limit_message_len") {
        mapped = aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_LIMIT_MESSAGE_LEN;
    }

    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_driving_status_data()->set_status(mapped);

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, indication);
}

void SensorHandler::sendNightModeIndication(const nlohmann::json& nightMode) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) {
        AASDK_LOG(error) << "[LiteSensor] Cannot send night mode: channel not open";
        return;
    }

    if (!nightMode.contains("enabled") || !nightMode["enabled"].is_boolean()) {
        AASDK_LOG(error) << "[LiteSensor] Night mode json missing enabled boolean";
        return;
    }

    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_night_mode_data()->set_night_mode(nightMode["enabled"].get<bool>());

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, indication);
}

void SensorHandler::sendLocationIndication(const nlohmann::json& location) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) {
        AASDK_LOG(error) << "[LiteSensor] Cannot send location: channel not open";
        return;
    }

    if (!location.contains("latitude") || !location.contains("longitude") ||
        !location["latitude"].is_number() || !location["longitude"].is_number()) {
        AASDK_LOG(error) << "[LiteSensor] Location json missing latitude/longitude";
        return;
    }

    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    auto* loc = indication.add_location_data();

    loc->set_latitude_e7(std::llround(location["latitude"].get<double>() * 1e7));
    loc->set_longitude_e7(std::llround(location["longitude"].get<double>() * 1e7));

    if (location.contains("accuracy_m") && location["accuracy_m"].is_number()) {
        loc->set_accuracy_e3(std::llround(location["accuracy_m"].get<double>() * 1e3));
    }
    if (location.contains("altitude_m") && location["altitude_m"].is_number()) {
        loc->set_altitude_e2(std::llround(location["altitude_m"].get<double>() * 1e2));
    }
    if (location.contains("speed_mps") && location["speed_mps"].is_number()) {
        loc->set_speed_e3(std::llround(location["speed_mps"].get<double>() * 1e3));
    }
    if (location.contains("bearing_deg") && location["bearing_deg"].is_number()) {
        loc->set_bearing_e6(std::llround(location["bearing_deg"].get<double>() * 1e6));
    }

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, indication);

    AASDK_LOG(debug) << "[LiteSensor] sent location: " << loc->ShortDebugString();
}

void SensorHandler::sendProto(messenger::ChannelId ch,
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
