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

#pragma once

#include <aap_protobuf/service/sensorsource/message/DrivingStatus.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>
#include <Channel/SensorSource/SensorSourceService.hpp>
#include <f1x/openauto/autoapp/Service/IService.hpp>
#include <boost/asio.hpp>
#include <Messenger/IMessenger.hpp>

using IoContext = boost::asio::io_context;
using Strand = boost::asio::strand<IoContext::executor_type>;

namespace f1x::openauto::autoapp::service::sensor {
  class SensorService :
      public aasdk::channel::sensorsource::ISensorSourceServiceEventHandler,
      public IService,
      public std::enable_shared_from_this<SensorService> {
  public:
    SensorService(IoContext &ioContext,
                  aasdk::messenger::IMessenger::Pointer messenger);

    bool isNight = false;
    bool previous = false;
    bool stopPolling = false;

    void start() override;

    void stop() override;

    void pause() override;

    void resume() override;

    void fillFeatures(aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) override;

    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) override;

    void onSensorStartRequest(
        const aap_protobuf::service::sensorsource::message::SensorRequest &request) override;

    void onChannelError(const aasdk::error::Error &e) override;

  private:
    using std::enable_shared_from_this<SensorService>::shared_from_this;

    void sendDrivingStatusUnrestricted();

    void sendNightData();

    void sendMockGPSLocationData(); // Mocked GPS data

    bool is_file_exist(const char *filename);

    void sensorPolling();

    bool firstRun = true;

    Strand strand_;
    boost::asio::deadline_timer timer_; // Reordered to match initialization order
    aasdk::channel::sensorsource::SensorSourceService::Pointer channel_;
  };

}



