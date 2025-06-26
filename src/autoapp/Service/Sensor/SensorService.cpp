/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <fstream>
#include <cmath>
#include <chrono>
#include <boost/date_time/posix_time/posix_time.hpp>

// Uncomment the line below to enable detailed debug logging for this service
// #define OAE_SENSOR_SERVICE_DEBUG

#ifdef OAE_SENSOR_SERVICE_DEBUG
#define SENSOR_LOG_DEBUG(x) OPENAUTO_LOG(debug) << "[SensorService] " << x
#else
#define SENSOR_LOG_DEBUG(x) do {} while (0)
#endif

namespace f1x::openauto::autoapp::service::sensor {
  SensorService::SensorService(boost::asio::io_context &ioContext,
                               aasdk::messenger::IMessenger::Pointer messenger)
      : strand_(ioContext.get_executor()),
        timer_(ioContext),
        channel_(std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(strand_, std::move(messenger))) {

  }

  void SensorService::start() {
    boost::asio::dispatch([this, self = this->shared_from_this()]() {
      if (is_file_exist("/tmp/night_mode_enabled")) {
        this->isNight = true;
      }
      this->sensorPolling();

      OPENAUTO_LOG(info) << "[SensorService] start()";
      channel_->receive(this->shared_from_this());
    });
  }

  void SensorService::stop() {
    this->stopPolling = true;

    boost::asio::dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[SensorService] stop()";
    });
  }

  void SensorService::pause() {
    boost::asio::dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[SensorService] pause()";
    });
  }

  void SensorService::resume() {
    boost::asio::dispatch([this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[SensorService] resume()";
    });
  }

  void SensorService::fillFeatures(
      aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
    OPENAUTO_LOG(info) << "[SensorService] fillFeatures()";

    auto *service = response.add_channels();
    service->set_id(static_cast<uint32_t>(channel_->getId()));

    auto *sensorChannel = service->mutable_sensor_source_service();
    sensorChannel->add_sensors()->set_sensor_type(
        aap_protobuf::service::sensorsource::message::SensorType::SENSOR_DRIVING_STATUS_DATA);
    sensorChannel->add_sensors()->set_sensor_type(
        aap_protobuf::service::sensorsource::message::SensorType::SENSOR_LOCATION);
    sensorChannel->add_sensors()->set_sensor_type(
        aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE);
  }

  void SensorService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
    OPENAUTO_LOG(info) << "[SensorService] onChannelOpenRequest()";
    SENSOR_LOG_DEBUG("Channel Id: " << request.service_id() << ", Priority: " << request.priority());

    aap_protobuf::service::control::message::ChannelOpenResponse response;
    const aap_protobuf::shared::MessageStatus status = aap_protobuf::shared::MessageStatus::STATUS_SUCCESS;
    response.set_status(status);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendChannelOpenResponse(response, std::move(promise));

    channel_->receive(this->shared_from_this());
  }

  void SensorService::onSensorStartRequest(
      const aap_protobuf::service::sensorsource::message::SensorRequest &request) {
    OPENAUTO_LOG(info) << "[SensorService] onSensorStartRequest()";
    SENSOR_LOG_DEBUG("Request Type: " << request.type());

    aap_protobuf::service::sensorsource::message::SensorStartResponseMessage response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    auto promise = aasdk::channel::SendPromise::defer(strand_);

    if (request.type() == aap_protobuf::service::sensorsource::message::SENSOR_DRIVING_STATUS_DATA) {
      promise->then(std::bind(&SensorService::sendDrivingStatusUnrestricted, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE) {
      promise->then(std::bind(&SensorService::sendNightData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_LOCATION) {
      promise->then(std::bind(&SensorService::sendMockGPSLocationData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else {
      promise->then([]() {},
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    }

    channel_->sendSensorStartResponse(response, std::move(promise));
    channel_->receive(this->shared_from_this());
  }

  void SensorService::sendDrivingStatusUnrestricted() {
    SENSOR_LOG_DEBUG("sendDrivingStatusUnrestricted()");
    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_driving_status_data()->set_status(
        aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sendNightData() {
    SENSOR_LOG_DEBUG("sendNightData()");
    aap_protobuf::service::sensorsource::message::SensorBatch indication;

    indication.add_night_mode_data()->set_night_mode(isNight);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
    if (this->firstRun) {
      this->firstRun = false;
    }
  }

  void SensorService::sendMockGPSLocationData() {
    SENSOR_LOG_DEBUG("sendMockGPSLocationData() [MOCKED]");
    aap_protobuf::service::sensorsource::message::SensorBatch indication;

    auto *locInd = indication.add_location_data();

    // Mocked values
    locInd->set_latitude_e7(static_cast<int32_t>(37.7749 * 1e7));  // San Francisco latitude
    locInd->set_longitude_e7(static_cast<int32_t>(-122.4194 * 1e7)); // San Francisco longitude
    locInd->set_accuracy_e3(5000); // 5km accuracy (mocked)
    locInd->set_altitude_e2(1500); // 15 meters in centimeters
    locInd->set_speed_e3(0);       // 0 mm/s (stationary)
    locInd->set_bearing_e6(0);     // 0 millionths of a degree (north)

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sensorPolling() {
    SENSOR_LOG_DEBUG("sensorPolling()");
    if (!this->stopPolling) {
      boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
        this->isNight = is_file_exist("/tmp/night_mode_enabled");
        if (this->previous != this->isNight && !this->firstRun) {
          this->previous = this->isNight;
          this->sendNightData();
        }

        timer_.expires_from_now(boost::posix_time::milliseconds(250));
        timer_.async_wait(boost::asio::bind_executor(strand_, std::bind(&SensorService::sensorPolling, this->shared_from_this())));
      });
    }
  }

  bool SensorService::is_file_exist(const char *fileName) {
    SENSOR_LOG_DEBUG("is_file_exist() check for: " << fileName);
    std::ifstream ifile(fileName, std::ios::in);
    return ifile.good();
  }

  void SensorService::onChannelError(const aasdk::error::Error &e) {
    OPENAUTO_LOG(error) << "[SensorService] onChannelError(): " << e.what();
  }
}



