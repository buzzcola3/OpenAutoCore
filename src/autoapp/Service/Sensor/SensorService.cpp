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
// #include <gps.h> // GPSD dependency removed
#include <chrono>  // Added for timestamp

namespace f1x::openauto::autoapp::service::sensor {
  SensorService::SensorService(boost::asio::io_context &ioContext,
                               aasdk::messenger::IMessenger::Pointer messenger)
      : strand_(ioContext.get_executor()),
        timer_(ioContext),
        channel_(std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(strand_, std::move(messenger))) {

  }

  void SensorService::start() {
    boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
      /*
      if (gps_open("127.0.0.1", "2947", &this->gpsData_)) {
        OPENAUTO_LOG(warning) << "[SensorService] can't connect to GPSD.";
      } else {
        OPENAUTO_LOG(info) << "[SensorService] Connected to GPSD.";
        gps_stream(&this->gpsData_, WATCH_ENABLE | WATCH_JSON, NULL);
        this->gpsEnabled_ = true;
      }
      */
      OPENAUTO_LOG(info) << "[SensorService] Using mocked GPS data.";

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

    boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
      /*
      if (this->gpsEnabled_) {
        gps_stream(&this->gpsData_, WATCH_DISABLE, NULL);
        gps_close(&this->gpsData_);
        this->gpsEnabled_ = false;
      }
      */

      OPENAUTO_LOG(info) << "[SensorService] stop()";
    });
  }

  void SensorService::pause() {
    boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
      OPENAUTO_LOG(info) << "[SensorService] pause()";
    });
  }

  void SensorService::resume() {
    boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
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
    OPENAUTO_LOG(debug) << "[SensorService] Channel Id: " << request.service_id() << ", Priority: "
                        << request.priority();

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
    OPENAUTO_LOG(debug) << "[SensorService] Request Type: " << request.type();

    aap_protobuf::service::sensorsource::message::SensorStartResponseMessage response;
    response.set_status(aap_protobuf::shared::MessageStatus::STATUS_SUCCESS);

    auto promise = aasdk::channel::SendPromise::defer(strand_);

    if (request.type() == aap_protobuf::service::sensorsource::message::SENSOR_DRIVING_STATUS_DATA) {
      promise->then(std::bind(&SensorService::sendDrivingStatusUnrestricted, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else if (request.type() == aap_protobuf::service::sensorsource::message::SensorType::SENSOR_NIGHT_MODE) {
      promise->then(std::bind(&SensorService::sendNightData, this->shared_from_this()),
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    } else {
      promise->then([]() {},
                    std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    }

    channel_->sendSensorStartResponse(response, std::move(promise));
    channel_->receive(this->shared_from_this());
  }

  void SensorService::sendDrivingStatusUnrestricted() {
    OPENAUTO_LOG(info) << "[SensorService] sendDrivingStatusUnrestricted()";
    aap_protobuf::service::sensorsource::message::SensorBatch indication;
    indication.add_driving_status_data()->set_status(
        aap_protobuf::service::sensorsource::message::DrivingStatus::DRIVE_STATUS_UNRESTRICTED);

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sendNightData() {
    OPENAUTO_LOG(info) << "[SensorService] sendNightData()";
    aap_protobuf::service::sensorsource::message::SensorBatch indication;

    if (SensorService::isNight) {
      OPENAUTO_LOG(info) << "[SensorService] Night Mode Triggered";
      indication.add_night_mode_data()->set_night_mode(true);
    } else {
      OPENAUTO_LOG(info) << "[SensorService] Day Mode Triggered";
      indication.add_night_mode_data()->set_night_mode(false);
    }

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
    if (this->firstRun) {
      this->firstRun = false;
      this->previous = this->isNight;
    }
  }

  void SensorService::sendGPSLocationData() {
    OPENAUTO_LOG(trace) << "[SensorService] sendGPSLocationData()";
    aap_protobuf::service::sensorsource::message::SensorBatch indication;

    auto *locInd = indication.add_location_data();

    // --- MOCK GPS DATA START ---
    static double mock_latitude = 34.052235;
    static double mock_longitude = -118.243683;

    // Simulate movement
    mock_latitude += 0.00001;
    mock_longitude += 0.00001;

    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto value = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    locInd->set_timestamp(value.count());

    locInd->set_latitude_e7(mock_latitude * 1e7);
    locInd->set_longitude_e7(mock_longitude * 1e7);
    locInd->set_accuracy_e3(10 * 1e3); // 10 meters
    locInd->set_altitude_e2(50 * 1e2); // 50 meters
    locInd->set_speed_e3(5 * 1e3); // 5 m/s
    locInd->set_bearing_e6(45 * 1e6); // 45 degrees
    // --- MOCK GPS DATA END ---

    auto promise = aasdk::channel::SendPromise::defer(strand_);
    promise->then([]() {},
                  std::bind(&SensorService::onChannelError, this->shared_from_this(), std::placeholders::_1));
    channel_->sendSensorEventIndication(indication, std::move(promise));
  }

  void SensorService::sensorPolling() {
    OPENAUTO_LOG(trace) << "[SensorService] sensorPolling()";
    if (!this->stopPolling) {
      boost::asio::dispatch(strand_, [this, self = this->shared_from_this()]() {
        this->isNight = is_file_exist("/tmp/night_mode_enabled");
        if (this->previous != this->isNight && !this->firstRun) {
          this->previous = this->isNight;
          this->sendNightData();
        }

        // Always send mock GPS data instead of checking gpsd
        this->sendGPSLocationData();

        timer_.expires_from_now(boost::posix_time::milliseconds(250));
        timer_.async_wait(boost::asio::bind_executor(strand_, [this, self = shared_from_this()](const boost::system::error_code& ec){
            if (!ec) {
                this->sensorPolling();
            }
        }));
      });
    }
  }

  bool SensorService::is_file_exist(const char *fileName) {
    OPENAUTO_LOG(info) << "[SensorService] is_file_exist()";
    std::ifstream ifile(fileName, std::ios::in);
    return ifile.good();
  }

  void SensorService::onChannelError(const aasdk::error::Error &e) {
    OPENAUTO_LOG(error) << "[SensorService] onChannelError(): " << e.what();
  }
}


