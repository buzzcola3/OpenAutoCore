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

#include <Common/MessageType.hpp>
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
#include <aap_protobuf/service/sensorsource/message/Gear.pb.h>
#include <aap_protobuf/service/sensorsource/message/HeadLightState.pb.h>
#include <aap_protobuf/service/sensorsource/message/TurnIndicatorState.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

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

    static const std::pair<const char*, void(SensorHandler::*)(const nlohmann::json&)> kOptionalSensors[] = {
        {"compass",          &SensorHandler::sendCompassIndication},
        {"speed",            &SensorHandler::sendSpeedIndication},
        {"rpm",              &SensorHandler::sendRpmIndication},
        {"odometer",         &SensorHandler::sendOdometerIndication},
        {"fuel",             &SensorHandler::sendFuelIndication},
        {"parking_brake",    &SensorHandler::sendParkingBrakeIndication},
        {"gear",             &SensorHandler::sendGearIndication},
        {"environment",      &SensorHandler::sendEnvironmentIndication},
        {"hvac",             &SensorHandler::sendHvacIndication},
        {"dead_reckoning",   &SensorHandler::sendDeadReckoningIndication},
        {"passenger",        &SensorHandler::sendPassengerIndication},
        {"door",             &SensorHandler::sendDoorIndication},
        {"light",            &SensorHandler::sendLightIndication},
        {"tire_pressure",    &SensorHandler::sendTirePressureIndication},
        {"accelerometer",    &SensorHandler::sendAccelerometerIndication},
        {"gyroscope",        &SensorHandler::sendGyroscopeIndication},
        {"gps_satellite",    &SensorHandler::sendGpsSatelliteIndication},
        {"toll_card",        &SensorHandler::sendTollCardIndication},
    };

    for (const auto& [key, fn] : kOptionalSensors) {
        if (json.contains(key) && json[key].is_object()) {
            (this->*fn)(json[key]);
            handled = true;
        }
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

void SensorHandler::sendCompassIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_compass_data();
    d->set_bearing_e6(std::llround(j.value("bearing_deg", 0.0) * 1e6));
    if (j.contains("pitch_deg"))
        d->set_pitch_e6(std::llround(j["pitch_deg"].get<double>() * 1e6));
    if (j.contains("roll_deg"))
        d->set_roll_e6(std::llround(j["roll_deg"].get<double>() * 1e6));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendSpeedIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_speed_data();
    d->set_speed_e3(std::llround(j.value("speed_mps", 0.0) * 1e3));
    if (j.contains("cruise_engaged"))
        d->set_cruise_engaged(j["cruise_engaged"].get<bool>());
    if (j.contains("cruise_set_speed_mps"))
        d->set_cruise_set_speed(std::llround(j["cruise_set_speed_mps"].get<double>() * 1e3));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendRpmIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    batch.add_rpm_data()->set_rpm_e3(std::llround(j.value("rpm", 0.0) * 1e3));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendOdometerIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_odometer_data();
    d->set_kms_e1(std::llround(j.value("kms", 0.0) * 10.0));
    if (j.contains("trip_kms"))
        d->set_trip_kms_e1(std::llround(j["trip_kms"].get<double>() * 10.0));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendFuelIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_fuel_data();
    if (j.contains("level"))
        d->set_fuel_level(j["level"].get<int>());
    if (j.contains("range"))
        d->set_range(j["range"].get<int>());
    if (j.contains("low_fuel_warning"))
        d->set_low_fuel_warning(j["low_fuel_warning"].get<bool>());

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendParkingBrakeIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    batch.add_parking_brake_data()->set_parking_brake(j.value("engaged", false));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendGearIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    using G = aap_protobuf::service::sensorsource::message::Gear;
    static const std::unordered_map<std::string, G> kGearMap = {
        {"neutral", G::GEAR_NEUTRAL}, {"N", G::GEAR_NEUTRAL},
        {"1", G::GEAR_1}, {"2", G::GEAR_2}, {"3", G::GEAR_3},
        {"4", G::GEAR_4}, {"5", G::GEAR_5}, {"6", G::GEAR_6},
        {"7", G::GEAR_7}, {"8", G::GEAR_8}, {"9", G::GEAR_9}, {"10", G::GEAR_10},
        {"drive", G::GEAR_DRIVE}, {"D", G::GEAR_DRIVE},
        {"park", G::GEAR_PARK}, {"P", G::GEAR_PARK},
        {"reverse", G::GEAR_REVERSE}, {"R", G::GEAR_REVERSE},
    };

    auto gear = G::GEAR_NEUTRAL;
    if (j.contains("gear") && j["gear"].is_string()) {
        auto it = kGearMap.find(j["gear"].get<std::string>());
        if (it != kGearMap.end()) gear = it->second;
    }

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    batch.add_gear_data()->set_gear(gear);

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendEnvironmentIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_environment_data();
    if (j.contains("temperature_c"))
        d->set_temperature_e3(std::llround(j["temperature_c"].get<double>() * 1e3));
    if (j.contains("pressure_kpa"))
        d->set_pressure_e3(std::llround(j["pressure_kpa"].get<double>() * 1e3));
    if (j.contains("rain"))
        d->set_rain(j["rain"].get<int>());

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendHvacIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_hvac_data();
    if (j.contains("target_temperature_c"))
        d->set_target_temperature_e3(std::llround(j["target_temperature_c"].get<double>() * 1e3));
    if (j.contains("current_temperature_c"))
        d->set_current_temperature_e3(std::llround(j["current_temperature_c"].get<double>() * 1e3));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendDeadReckoningIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_dead_reckoning_data();
    if (j.contains("steering_angle_deg"))
        d->set_steering_angle_e1(std::llround(j["steering_angle_deg"].get<double>() * 10.0));
    if (j.contains("wheel_speeds_mps") && j["wheel_speeds_mps"].is_array()) {
        for (const auto& ws : j["wheel_speeds_mps"])
            d->add_wheel_speed_e3(std::llround(ws.get<double>() * 1e3));
    }

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendPassengerIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    batch.add_passenger_data()->set_passenger_present(j.value("present", false));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendDoorIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_door_data();
    if (j.contains("hood_open"))
        d->set_hood_open(j["hood_open"].get<bool>());
    if (j.contains("trunk_open"))
        d->set_trunk_open(j["trunk_open"].get<bool>());
    if (j.contains("doors") && j["doors"].is_array()) {
        for (const auto& door : j["doors"])
            d->add_door_open(door.get<bool>());
    }

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendLightIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    using HL = aap_protobuf::service::sensorsource::message::HeadLightState;
    using TI = aap_protobuf::service::sensorsource::message::TurnIndicatorState;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_light_data();

    if (j.contains("headlight") && j["headlight"].is_string()) {
        const auto& v = j["headlight"].get<std::string>();
        if (v == "off")       d->set_head_light_state(HL::HEAD_LIGHT_STATE_OFF);
        else if (v == "on")   d->set_head_light_state(HL::HEAD_LIGHT_STATE_ON);
        else if (v == "high") d->set_head_light_state(HL::HEAD_LIGHT_STATE_HIGH);
    }
    if (j.contains("turn_indicator") && j["turn_indicator"].is_string()) {
        const auto& v = j["turn_indicator"].get<std::string>();
        if (v == "none")       d->set_turn_indicator_state(TI::TURN_INDICATOR_NONE);
        else if (v == "left")  d->set_turn_indicator_state(TI::TURN_INDICATOR_LEFT);
        else if (v == "right") d->set_turn_indicator_state(TI::TURN_INDICATOR_RIGHT);
    }
    if (j.contains("hazard_lights"))
        d->set_hazard_lights_on(j["hazard_lights"].get<bool>());

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendTirePressureIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_tire_pressure_data();
    if (j.contains("pressures_kpa") && j["pressures_kpa"].is_array()) {
        for (const auto& p : j["pressures_kpa"])
            d->add_tire_pressures_e2(std::llround(p.get<double>() * 1e2));
    }

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendAccelerometerIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_accelerometer_data();
    if (j.contains("x")) d->set_acceleration_x_e3(std::llround(j["x"].get<double>() * 1e3));
    if (j.contains("y")) d->set_acceleration_y_e3(std::llround(j["y"].get<double>() * 1e3));
    if (j.contains("z")) d->set_acceleration_z_e3(std::llround(j["z"].get<double>() * 1e3));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendGyroscopeIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_gyroscope_data();
    if (j.contains("x")) d->set_rotation_speed_x_e3(std::llround(j["x"].get<double>() * 1e3));
    if (j.contains("y")) d->set_rotation_speed_y_e3(std::llround(j["y"].get<double>() * 1e3));
    if (j.contains("z")) d->set_rotation_speed_z_e3(std::llround(j["z"].get<double>() * 1e3));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendGpsSatelliteIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    auto* d = batch.add_gps_satellite_data();
    d->set_number_in_use(j.value("in_use", 0));
    if (j.contains("in_view"))
        d->set_number_in_view(j["in_view"].get<int>());
    if (j.contains("satellites") && j["satellites"].is_array()) {
        for (const auto& s : j["satellites"]) {
            auto* sat = d->add_satellites();
            sat->set_prn(s.value("prn", 0));
            sat->set_snr_e3(std::llround(s.value("snr", 0.0) * 1e3));
            sat->set_used_in_fix(s.value("used_in_fix", false));
            if (s.contains("azimuth_deg"))
                sat->set_azimuth_e3(std::llround(s["azimuth_deg"].get<double>() * 1e3));
            if (s.contains("elevation_deg"))
                sat->set_elevation_e3(std::llround(s["elevation_deg"].get<double>() * 1e3));
        }
    }

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
}

void SensorHandler::sendTollCardIndication(const nlohmann::json& j) {
    if (sensorChannelId_ == messenger::ChannelId::NONE) return;

    aap_protobuf::service::sensorsource::message::SensorBatch batch;
    batch.add_toll_card_data()->set_is_card_present(j.value("present", false));

    sendProto(sensorChannelId_, sensorEncryptionType_,
              messenger::MessageType::SPECIFIC,
              Sensor::SENSOR_MESSAGE_BATCH, batch);
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
