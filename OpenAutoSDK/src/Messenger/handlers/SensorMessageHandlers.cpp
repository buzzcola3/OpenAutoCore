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
#include <gps.h>
#include <fstream>
#include <limits>

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
    if (request.type() == aap_protobuf::service::sensorsource::message::SENSOR_DRIVING_STATUS_DATA) {
      sendDrivingStatusIndication(message);
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE) {
      sendNightModeIndication(message);
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_LOCATION) {
      sendLocationIndication(message);
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

  if (sender_ == nullptr || sensorChannelId_ == ::aasdk::messenger::ChannelId::NONE) {
    AASDK_LOG(error) << kLogPrefix << " Cannot send SensorBatch: sender or channel unavailable.";
    return;
  }

  aap_protobuf::service::sensorsource::message::SensorBatch batch;
  if (!parsePayload(static_cast<const std::uint8_t*>(data), size, batch, "SensorBatch")) {
    return;
  }

  sender_->sendProtobuf(sensorChannelId_,
                        sensorEncryptionType_,
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Sensor::SENSOR_MESSAGE_BATCH,
                        batch);

  AASDK_LOG(debug) << kLogPrefix << " forwarded transport SensorBatch ts=" << timestamp
                   << " bytes=" << size;
}

bool SensorMessageHandlers::sendDrivingStatusIndication(const ::aasdk::messenger::Message& message) const {
  if (sender_ == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send driving status indication.";
    return false;
  }

  aap_protobuf::service::sensorsource::message::SensorBatch indication;
  indication.add_driving_status_data()->set_status(
      aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED);

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Sensor::SENSOR_MESSAGE_BATCH,
                        indication);
  return true;
}

bool SensorMessageHandlers::sendNightModeIndication(const ::aasdk::messenger::Message& message) const {
  if (sender_ == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send night mode indication.";
    return false;
  }

  const bool isNight = []() {
    std::ifstream ifile("/tmp/night_mode_enabled", std::ios::in);
    return ifile.good();
  }();

  aap_protobuf::service::sensorsource::message::SensorBatch indication;
  indication.add_night_mode_data()->set_night_mode(isNight);

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Sensor::SENSOR_MESSAGE_BATCH,
                        indication);
  return true;
}

bool SensorMessageHandlers::sendLocationIndication(const ::aasdk::messenger::Message& message) const {
  if (sender_ == nullptr) {
    AASDK_LOG(error) << kLogPrefix << " MessageSender not configured; cannot send location indication.";
    return false;
  }

  struct gps_data_t gpsData {};
  if (gps_open("127.0.0.1", "2947", &gpsData) != 0) {
    AASDK_LOG(warning) << kLogPrefix << " gps_open failed; cannot send location.";
    return false;
  }

  const auto shutdownGps = [&gpsData]() {
    gps_stream(&gpsData, WATCH_DISABLE, nullptr);
    gps_close(&gpsData);
  };

  gps_stream(&gpsData, WATCH_ENABLE | WATCH_JSON, nullptr);

  bool gpsReadOk = false;
#if GPSD_API_MAJOR_VERSION >= 7
  if (gps_read(&gpsData, nullptr, 0) != -1) {
    gpsReadOk = true;
  }
#else
  if (gps_read(&gpsData) != -1) {
    gpsReadOk = true;
  }
#endif

  if (!gpsReadOk) {
    AASDK_LOG(warning) << kLogPrefix << " gps_read failed; cannot send location.";
    shutdownGps();
    return false;
  }

  if ((gpsData.fix.mode != MODE_2D && gpsData.fix.mode != MODE_3D) ||
      !(gpsData.set & TIME_SET) ||
      !(gpsData.set & LATLON_SET)) {
    AASDK_LOG(debug) << kLogPrefix << " GPS fix not ready; skipping location indication.";
    shutdownGps();
    return false;
  }

  aap_protobuf::service::sensorsource::message::SensorBatch indication;
  auto* locInd = indication.add_location_data();

#if GPSD_API_MAJOR_VERSION >= 7
  locInd->set_timestamp(gpsData.fix.time.tv_sec);
#else
  locInd->set_timestamp(gpsData.fix.time);
#endif
  locInd->set_latitude_e7(gpsData.fix.latitude * 1e7);
  locInd->set_longitude_e7(gpsData.fix.longitude * 1e7);

  const auto accuracy = std::sqrt(std::pow(gpsData.fix.epx, 2) + std::pow(gpsData.fix.epy, 2));
  locInd->set_accuracy_e3(accuracy * 1e3);

  if (gpsData.set & ALTITUDE_SET) {
    locInd->set_altitude_e2(gpsData.fix.altitude * 1e2);
  }
  if (gpsData.set & SPEED_SET) {
    locInd->set_speed_e3(gpsData.fix.speed * 1.94384 * 1e3);
  }
  if (gpsData.set & TRACK_SET) {
    locInd->set_bearing_e6(gpsData.fix.track * 1e6);
  }

  sender_->sendProtobuf(message.getChannelId(),
                        message.getEncryptionType(),
                        ::aasdk::messenger::MessageType::SPECIFIC,
                        Sensor::SENSOR_MESSAGE_BATCH,
                        indication);

  shutdownGps();
  return true;
}

}
