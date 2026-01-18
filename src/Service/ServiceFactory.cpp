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

#include <Service/ServiceFactory.hpp>
#include <Common/Log.hpp>
#include <Common/Log.hpp>

namespace f1x::openauto::autoapp::service {

  ServiceFactory::ServiceFactory(boost::asio::io_service &ioService,
                                 configuration::IConfiguration::Pointer configuration)
      : ioService_(ioService), configuration_(std::move(configuration)) {

  }

  ServiceList ServiceFactory::create(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] create()";
    ServiceList serviceList;

    if (configuration_->getWirelessProjectionEnabled()) {
      // TODO: What is WiFi Projection Service?
      /*
       * The btservice seems to handle connecting over bluetooth and allow AA to establish a WiFi connection for Projection
       * If WifiProjection is a legitimate service, then it seems clear it is not what we think it actually is.
       */
      // serviceList.emplace_back(this->createWifiProjectionService(messenger));
    }

    return serviceList;
  }

  std::shared_ptr<buzz::autoapp::Transport::Transport> ServiceFactory::getTransport() {
    if (transport_ == nullptr) {
      transport_ = std::make_shared<buzz::autoapp::Transport::Transport>();
    }
    return transport_;
  }

}



