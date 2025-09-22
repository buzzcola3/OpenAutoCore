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

#include <f1x/openauto/autoapp/Service/MediaSink/VideoService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/MediaAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/GuidanceAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/SystemAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/TelephonyAudioService.hpp>

#include <f1x/openauto/autoapp/Service/MediaSource/MicrophoneMediaSourceService.hpp>

#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <f1x/openauto/autoapp/Service/Bluetooth/BluetoothService.hpp>
#include <f1x/openauto/autoapp/Service/InputSource/InputSourceService.hpp>
#include <f1x/openauto/autoapp/Projection/DummyBluetoothDevice.hpp>
#include <f1x/openauto/autoapp/Projection/InputDevice.hpp>
#include <f1x/openauto/autoapp/Projection/SharedAudioOutput.hpp>
#include <f1x/openauto/autoapp/Projection/SharedVideoOutput.hpp>
#include <f1x/openauto/autoapp/Projection/SharedAudioInput.hpp>

#include <buzz/common/Rect.hpp>
#include "open_auto_transport/transport.hpp"

namespace f1x {
namespace openauto {
namespace autoapp {
namespace service {

ServiceFactory::ServiceFactory(IoContext &ioContext,
                               configuration::IConfiguration::Pointer configuration,
                               std::shared_ptr<buzz::autoapp::Transport::Transport> transport)
  : ioContext_(ioContext)
  , configuration_(std::move(configuration))
  , transport_(std::move(transport)) {

  if (transport_) {
    // Start as Side A (creator). Clean stale segments once.
    if (!transport_->isRunning()) {
      bool ok = transport_->startAsA(std::chrono::microseconds{1000}, /*clean=*/true);
      if (!ok) {
        std::cerr << "[ServiceFactory] Failed to start shared memory transport as A.\n";
      } else {
        std::cout << "[ServiceFactory] Transport started as A.\n";
      }
    }
  }
}

ServiceList ServiceFactory::create(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] create()";
    ServiceList serviceList;

    this->createMediaSinkServices(serviceList, messenger);
    this->createMediaSourceServices(serviceList, messenger);
    serviceList.emplace_back(this->createSensorService(messenger));
    serviceList.emplace_back(this->createInputService(messenger));
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

  IService::Pointer ServiceFactory::createBluetoothService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createBluetoothService()";
    projection::IBluetoothDevice::Pointer bluetoothDevice;
    if (configuration_->getBluetoothAdapterAddress().empty()) { // Changed to use .empty()
      OPENAUTO_LOG(debug) << "[ServiceFactory] Using Dummy Bluetooth";
      bluetoothDevice = std::make_shared<projection::DummyBluetoothDevice>();
    } else {
      OPENAUTO_LOG(info) << "[ServiceFactory] Using Local Bluetooth Adapter (STUBBED)";
      // Original Qt-dependent code for LocalBluetoothDevice is removed.
      // Using DummyBluetoothDevice as a stub.
      bluetoothDevice = std::make_shared<projection::DummyBluetoothDevice>();
    }

    return std::make_shared<bluetooth::BluetoothService>(ioContext_, messenger, std::move(bluetoothDevice)); // Changed ioContext_ to ioService_
  }

  IService::Pointer ServiceFactory::createInputService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createInputService()";
    buzz::common::Rect videoGeometry;
    switch (configuration_->getVideoResolution()) {
      case aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_1280x720:
        OPENAUTO_LOG(info) << "[ServiceFactory] Resolution 1280x720";
        videoGeometry = buzz::common::Rect(0, 0, 1280, 720);
        break;
      case aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_1920x1080:
        OPENAUTO_LOG(info) << "[ServiceFactory] Resolution 1920x1080";
        videoGeometry = buzz::common::Rect(0, 0, 1920, 1080);
        break;
      default:
        OPENAUTO_LOG(info) << "[ServiceFactory] Resolution 800x480";
        videoGeometry = buzz::common::Rect(0, 0, 800, 480);
        break;
    }

    // Stubbing inputDevice
    // The original code for creating inputDevice is commented out.
    // We'll pass nullptr. InputSourceService needs to be able to handle this.
    auto inputDevice = std::make_shared<projection::InputDevice>(configuration_, videoGeometry, videoGeometry);

    // If InputSourceService cannot handle a nullptr, you might need a dummy implementation:
    // class DummyInputDevice : public projection::IInputDevice { /* ... implement pure virtuals ... */ };
    // inputDevice = std::make_shared<DummyInputDevice>();

    return std::make_shared<inputsource::InputSourceService>(ioContext_, messenger, std::move(inputDevice));
  }


  void ServiceFactory::createMediaSinkServices(ServiceList &serviceList,
                                               aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createMediaSinkServices()";

    // --- Media Audio Service ---
    if (1) {
        OPENAUTO_LOG(info) << "[ServiceFactory] Media Audio Channel enabled";
        auto mediaAudioOutput = std::make_shared<projection::SharedAudioOutput>(ioContext_, 2, 16, 48000);
        serviceList.emplace_back(
            std::make_shared<mediasink::MediaAudioService>(ioContext_, messenger, std::move(mediaAudioOutput)));
    }

    // --- Guidance Audio Service ---
    if (1) {
        OPENAUTO_LOG(info) << "[ServiceFactory] Guidance Audio Channel enabled";
        auto guidanceAudioOutput = std::make_shared<projection::SharedAudioOutput>(ioContext_, 1, 16, 16000);
        serviceList.emplace_back(
            std::make_shared<mediasink::GuidanceAudioService>(ioContext_, messenger, std::move(guidanceAudioOutput)));
    }

    // --- System Audio Service (Required by Android Auto) ---
    OPENAUTO_LOG(info) << "[ServiceFactory] System Audio Channel enabled";
    auto systemAudioOutput = std::make_shared<projection::SharedAudioOutput>(ioContext_, 1, 16, 16000);
    serviceList.emplace_back(
        std::make_shared<mediasink::SystemAudioService>(ioContext_, messenger, std::move(systemAudioOutput)));


    // --- Video Service Creation ---
    if (1) {
        OPENAUTO_LOG(info) << "[ServiceFactory] Video Channel enabled";

        auto videoOutput = std::make_shared<projection::SharedVideoOutput>(ioContext_, configuration_, transport_);

        serviceList.emplace_back(
            std::make_shared<mediasink::VideoService>(ioContext_, messenger, std::move(videoOutput)));
    }
  }

  void ServiceFactory::createMediaSourceServices(f1x::openauto::autoapp::service::ServiceList &serviceList,
                                                 aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createMediaSourceServices()";
    // Create the microphone service with our new SharedAudioInput class
    auto audioInput = std::make_shared<projection::SharedAudioInput>(1, 16, 16000);
    serviceList.emplace_back(std::make_shared<mediasource::MicrophoneMediaSourceService>(ioContext_, messenger, std::move(audioInput)));
  }

  IService::Pointer ServiceFactory::createSensorService(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[ServiceFactory] createSensorService()";
    return std::make_shared<sensor::SensorService>(ioContext_, messenger);
  }

} // namespace service
} // namespace autoapp
} // namespace openauto
} // namespace f1x



