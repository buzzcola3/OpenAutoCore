#include <Messenger/handlers/SensorMessageHandlers.hpp>

#include <Messenger/Message.hpp>
#include <Messenger/MessageId.hpp>
#include <Messenger/MessageSender.hpp>
#include <Messenger/MessageType.hpp>
#include <Common/Log.hpp>
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
#include <fstream>
#include <limits>
#include <string_view>

namespace {

using Control = aap_protobuf::service::control::message::ControlMessageType;
using Sensor = aap_protobuf::service::sensorsource::SensorMessageId;
constexpr const char* kLogPrefix = "[SensorMessageHandlers]";

template<typename Proto>
bool parsePayload(const std::uint8_t* data, std::size_t size, Proto& message, const char* label) {
  if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    AASDK_LOG(error) << kLogPrefix << " " << label << " payload too large, bytes=" << size;
    return false;
  }

  if (!message.ParseFromArray(data, static_cast<int>(size))) {
    AASDK_LOG(error) << kLogPrefix << " Failed to parse " << label << " payload, bytes=" << size;
    return false;
  }

  return true;
}

}

namespace aasdk::messenger::interceptor {

bool SensorMessageHandlers::handle(const ::aasdk::messenger::Message& message) const {
  ++messageCount_;
  const auto& rawPayload = message.getPayload();

  if (rawPayload.size() <= ::aasdk::messenger::MessageId::getSizeOf()) {
    AASDK_LOG(error) << kLogPrefix << " sensor payload too small";
    return false;
  }

  ::aasdk::messenger::MessageId messageId(rawPayload);
  const auto payloadSize = rawPayload.size() - ::aasdk::messenger::MessageId::getSizeOf();
  const auto* payloadData = rawPayload.data() + ::aasdk::messenger::MessageId::getSizeOf();

  switch (messageId.getId()) {
    case Control::MESSAGE_CHANNEL_OPEN_REQUEST:
      return handleChannelOpenRequest(message, payloadData, payloadSize);
    case Sensor::SENSOR_MESSAGE_REQUEST:
      return handleSensorStartRequest(message, payloadData, payloadSize);
    case Sensor::SENSOR_MESSAGE_RESPONSE:
      return handleSensorStopRequest(message, payloadData, payloadSize);
    default:
      AASDK_LOG(debug) << kLogPrefix << " message id=" << messageId.getId() << " not explicitly handled.";
      return false;
  }
}

void SensorMessageHandlers::setMessageSender(std::shared_ptr<::aasdk::messenger::MessageSender> sender) {
  sender_ = std::move(sender);
}

bool SensorMessageHandlers::handleChannelOpenRequest(const ::aasdk::messenger::Message& message,
                                                     const std::uint8_t* data,
                                                     std::size_t size) const {
  aap_protobuf::service::control::message::ChannelOpenRequest request;
  if (!parsePayload(data, size, request, "ChannelOpenRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " ChannelOpenRequest: " << request.ShortDebugString();

  aap_protobuf::service::control::message::ChannelOpenResponse response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  sensorChannelId_ = message.getChannelId();
  sensorEncryptionType_ = message.getEncryptionType();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::CONTROL,
                          Control::MESSAGE_CHANNEL_OPEN_RESPONSE,
                          response);
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send channel open response.";
  return false;
}

bool SensorMessageHandlers::handleSensorStartRequest(const ::aasdk::messenger::Message& message,
                                                     const std::uint8_t* data,
                                                     std::size_t size) const {
  aap_protobuf::service::sensorsource::message::SensorRequest request;
  if (!parsePayload(data, size, request, "SensorRequest")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " SensorRequest: " << request.ShortDebugString();

  aap_protobuf::service::sensorsource::message::SensorStartResponseMessage response;
  response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

  sensorChannelId_ = message.getChannelId();
  sensorEncryptionType_ = message.getEncryptionType();

  if (sender_ != nullptr) {
    sender_->sendProtobuf(message.getChannelId(),
                          message.getEncryptionType(),
                          ::aasdk::messenger::MessageType::SPECIFIC,
                          Sensor::SENSOR_MESSAGE_RESPONSE,
                          response);

    // Fire an initial indication matching the requested sensor type where possible.
    if (request.type() == aap_protobuf::service::sensorsource::message::SENSOR_DRIVING_STATUS_DATA ||
        request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE ||
        request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_LOCATION) {
      AASDK_LOG(debug) << kLogPrefix << " Sensor start request acknowledged; awaiting transport sensor JSON.";
    }
    return true;
  }

  AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send sensor start response.";
  return false;
}

bool SensorMessageHandlers::handleSensorStopRequest(const ::aasdk::messenger::Message& message,
                                                    const std::uint8_t* data,
                                                    std::size_t size) const {
  aap_protobuf::service::sensorsource::message::SensorResponse response;
  if (!parsePayload(data, size, response, "SensorResponse")) {
    return false;
  }

  AASDK_LOG(debug) << kLogPrefix << " SensorResponse (stop/ack): " << response.ShortDebugString();
  // No outbound response required; treat as handled.
  return true;
}

void SensorMessageHandlers::onSensorEvent(std::uint64_t timestamp,
                                          const void* data,
                                          std::size_t size) const {
  if (data == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " SENSOR payload nullptr";
    return;
  }

  const auto json = nlohmann::json::parse(
      std::string_view(static_cast<const char*>(data), size), nullptr, false);
  if (json.is_discarded()) {
    AASDK_LOG(error) << kLogPrefix << " Failed to parse SENSOR json ts=" << timestamp
                     << " bytes=" << size;
    return;
  }

  bool handled = false;

  if (json.contains("location") && json["location"].is_object()) {
    handled = sendLocationIndication(json["location"]);
  } else if (json.contains("location")) {
    AASDK_LOG(error) << kLogPrefix << " SENSOR json location is not an object";
    return;
  }

  if (json.contains("night_mode") && json["night_mode"].is_object()) {
    handled = sendNightModeIndication(json["night_mode"]) || handled;
  } else if (json.contains("night_mode")) {
    AASDK_LOG(error) << kLogPrefix << " SENSOR json night_mode is not an object";
    return;
  }

  if (json.contains("driving_status") && json["driving_status"].is_object()) {
    handled = sendDrivingStatusIndication(json["driving_status"]) || handled;
  } else if (json.contains("driving_status")) {
    AASDK_LOG(error) << kLogPrefix << " SENSOR json driving_status is not an object";
    return;
  }

  if (!handled) {
    AASDK_LOG(debug) << kLogPrefix << " SENSOR json contained no handled keys ts=" << timestamp;
  }
}

bool SensorMessageHandlers::sendDrivingStatusIndication(const nlohmann::json& drivingStatus) const {
  if (sender_ == nullptr || sensorChannelId_ == ::aasdk::messenger::ChannelId::NONE) {
    AASDK_LOG(error) << kLogPrefix << " Cannot send driving status indication; sender or channel unavailable.";
    return false;
  }

  if (!drivingStatus.contains("status") || !drivingStatus["status"].is_string()) {
    AASDK_LOG(error) << kLogPrefix << " Driving status json missing status.";
    return false;
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

  sender_->sendProtobuf(sensorChannelId_,
                        sensorEncryptionType_,
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Sensor::SENSOR_MESSAGE_BATCH,
                        indication);
  return true;
}

bool SensorMessageHandlers::sendNightModeIndication(const nlohmann::json& nightMode) const {
  if (sender_ == nullptr || sensorChannelId_ == ::aasdk::messenger::ChannelId::NONE) {
    AASDK_LOG(error) << kLogPrefix << " Cannot send night mode indication; sender or channel unavailable.";
    return false;
  }

  if (!nightMode.contains("enabled") || !nightMode["enabled"].is_boolean()) {
    AASDK_LOG(error) << kLogPrefix << " Night mode json missing enabled boolean.";
    return false;
  }

  aap_protobuf::service::sensorsource::message::SensorBatch indication;
  indication.add_night_mode_data()->set_night_mode(nightMode["enabled"].get<bool>());

  sender_->sendProtobuf(sensorChannelId_,
                        sensorEncryptionType_,
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Sensor::SENSOR_MESSAGE_BATCH,
                        indication);
  return true;
}

bool SensorMessageHandlers::sendLocationIndication(const nlohmann::json& location) const {
  if (sender_ == nullptr || sensorChannelId_ == ::aasdk::messenger::ChannelId::NONE) {
    AASDK_LOG(error) << kLogPrefix << " Cannot send location indication; sender or channel unavailable.";
    return false;
  }

  if (!location.contains("latitude") || !location.contains("longitude") ||
      !location["latitude"].is_number() || !location["longitude"].is_number()) {
    AASDK_LOG(error) << kLogPrefix << " Location json missing latitude/longitude.";
    return false;
  }

  aap_protobuf::service::sensorsource::message::SensorBatch indication;
  auto* locInd = indication.add_location_data();

  locInd->set_latitude_e7(std::llround(location["latitude"].get<double>() * 1e7));
  locInd->set_longitude_e7(std::llround(location["longitude"].get<double>() * 1e7));

  if (location.contains("accuracy_m") && location["accuracy_m"].is_number()) {
    locInd->set_accuracy_e3(std::llround(location["accuracy_m"].get<double>() * 1e3));
  }
  if (location.contains("altitude_m") && location["altitude_m"].is_number()) {
    locInd->set_altitude_e2(std::llround(location["altitude_m"].get<double>() * 1e2));
  }
  if (location.contains("speed_mps") && location["speed_mps"].is_number()) {
    locInd->set_speed_e3(std::llround(location["speed_mps"].get<double>() * 1e3));
  }
  if (location.contains("bearing_deg") && location["bearing_deg"].is_number()) {
    locInd->set_bearing_e6(std::llround(location["bearing_deg"].get<double>() * 1e6));
  }

  sender_->sendProtobuf(sensorChannelId_,
                        sensorEncryptionType_,
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Sensor::SENSOR_MESSAGE_BATCH,
                        indication);

  AASDK_LOG(debug) << kLogPrefix << " sent location indication: "
                   << locInd->ShortDebugString();

  return true;
}

}
