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

#include <Channel/MediaSink/Audio/Channel/MediaAudioChannel.hpp>
#include <Channel/MediaSink/Audio/Channel/SystemAudioChannel.hpp>
#include <Channel/MediaSink/Audio/Channel/GuidanceAudioChannel.hpp>
#include <Channel/MediaSink/Audio/Channel/TelephonyAudioChannel.hpp>

#include <f1x/openauto/autoapp/Service/ServiceFactory.hpp>

#include <f1x/openauto/autoapp/Service/MediaSink/MediaAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/GuidanceAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/SystemAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/TelephonyAudioService.hpp>

#include <f1x/openauto/autoapp/Service/MediaSource/MicrophoneMediaSourceService.hpp>

#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <f1x/openauto/autoapp/Service/Bluetooth/BluetoothService.hpp>
#include <f1x/openauto/autoapp/Projection/QtAudioOutput.hpp>
#include <f1x/openauto/autoapp/Projection/QtAudioInput.hpp>
#include <f1x/openauto/autoapp/Projection/LocalBluetoothDevice.hpp>
#include <f1x/openauto/autoapp/Projection/DummyBluetoothDevice.hpp>

namespace f1x::openauto::autoapp::service {

  ServiceFactory::ServiceFactory(boost::asio::io_service &ioService,
                                 configuration::IConfiguration::Pointer configuration)
      : ioService_(ioService), configuration_(std::move(configuration)) {

  }

  ServiceList ServiceFactory::create(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] create()";
    ServiceList serviceList;

    this->createMediaSinkServices(serviceList, messenger);
    this->createMediaSourceServices(serviceList, messenger);
    serviceList.emplace_back(this->createSensorService(messenger));
    if (configuration_->getWirelessProjectionEnabled())
    {
        // TODO: What is WiFi Projection Service?
        /*
         * The btservice seems to handle connecting over bluetooth and allow AA to establish a WiFi connection for Projection
         * If WifiProjection is a legitimate service, then it seems clear it is not what we think it actually is.
         */
        serviceList.emplace_back(this->createBluetoothService(messenger));
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

  IService::Pointer ServiceFactory::createBluetoothService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createBluetoothService()";
    projection::IBluetoothDevice::Pointer bluetoothDevice;
    if (configuration_->getBluetoothAdapterAddress() == "") {
      OPENAUTO_LOG(debug) << "[ServiceFactory] Using Dummy Bluetooth";
      bluetoothDevice = std::make_shared<projection::DummyBluetoothDevice>();
    } else {
      OPENAUTO_LOG(info) << "[ServiceFactory] Using Local Bluetooth Adapter";
      bluetoothDevice = projection::IBluetoothDevice::Pointer(new projection::LocalBluetoothDevice(),
                                                              std::bind(&QObject::deleteLater,
                                                                        std::placeholders::_1));
    }

    return std::make_shared<bluetooth::BluetoothService>(ioService_, messenger, std::move(bluetoothDevice));
  }

  void ServiceFactory::createMediaSinkServices(ServiceList &serviceList,
                                               aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createMediaSinkServices()";
    if (configuration_->musicAudioChannelEnabled()) {
      OPENAUTO_LOG(info) << "[ServiceFactory] Media Audio Channel enabled";
      auto mediaAudioOutput = projection::IAudioOutput::Pointer(new projection::QtAudioOutput(2, 16, 48000),
                                            std::bind(&QObject::deleteLater, std::placeholders::_1));

      serviceList.emplace_back(
          std::make_shared<mediasink::MediaAudioService>(ioService_, messenger, std::move(mediaAudioOutput)));
    }

    if (configuration_->guidanceAudioChannelEnabled()) {
      OPENAUTO_LOG(info) << "[ServiceFactory] Guidance Audio Channel enabled";
      auto guidanceAudioOutput = projection::IAudioOutput::Pointer(new projection::QtAudioOutput(1, 16, 16000),
                                            std::bind(&QObject::deleteLater, std::placeholders::_1));

      serviceList.emplace_back(
          std::make_shared<mediasink::GuidanceAudioService>(ioService_, messenger,
                                                            std::move(guidanceAudioOutput)));
    }

    /*
     * No Need to Check for systemAudioChannelEnabled - MUST be enabled by default.
     */

    OPENAUTO_LOG(info) << "[ServiceFactory] System Audio Channel enabled";
    auto systemAudioOutput = projection::IAudioOutput::Pointer(new projection::QtAudioOutput(1, 16, 16000),
                                          std::bind(&QObject::deleteLater, std::placeholders::_1));

    serviceList.emplace_back(
        std::make_shared<mediasink::SystemAudioService>(ioService_, messenger, std::move(systemAudioOutput)));

  }

  void ServiceFactory::createMediaSourceServices(f1x::openauto::autoapp::service::ServiceList &serviceList,
                                                 aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createMediaSourceServices()";
    projection::IAudioInput::Pointer audioInput(new projection::QtAudioInput(1, 16, 16000),
                                                std::bind(&QObject::deleteLater, std::placeholders::_1));
    serviceList.emplace_back(std::make_shared<mediasource::MicrophoneMediaSourceService>(ioService_, messenger,
                                                                                         std::move(audioInput)));
  }

  IService::Pointer ServiceFactory::createSensorService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createSensorService()";
    return std::make_shared<sensor::SensorService>(ioService_, messenger);
  }

}



