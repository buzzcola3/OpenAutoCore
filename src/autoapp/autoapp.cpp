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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fstream>
#include <mutex>
#include <thread>
#include <USB/USBHub.hpp>
#include <USB/ConnectedAccessoriesEnumerator.hpp>
#include <USB/AccessoryModeQueryChain.hpp>
#include <USB/AccessoryModeQueryChainFactory.hpp>
#include <USB/AccessoryModeQueryFactory.hpp>
#include <TCP/TCPWrapper.hpp>
#include <boost/log/utility/setup.hpp>
#include <App.hpp>
#include <Messenger/MessageInStreamInterceptor.hpp>
#include <Messenger/handlers/BluetoothMessageHandlers.hpp>
#include <Messenger/handlers/InputSourceMessageHandlers.hpp>
#include <Messenger/handlers/MediaSourceMessageHandlers.hpp>
#include <Messenger/handlers/SensorMessageHandlers.hpp>
#include <open_auto_transport/wire.hpp>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>
#include <Configuration/IConfiguration.hpp>
#include <Configuration/RecentAddressesList.hpp>
#include <Projection/IBluetoothDevice.hpp>
#include <Projection/BluezBluetoothDevice.hpp>
#include <Projection/DummyBluetoothDevice.hpp>
#include <Service/AndroidAutoEntityFactory.hpp>
#include <Service/ServiceFactory.hpp>
#include <Configuration/Configuration.hpp>
#include <Common/Log.hpp>
#include <btservice/BluetoothHandler.hpp>
#include <btservice/BluezBluetoothServer.hpp>

namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;

namespace {
    std::atomic_bool gRunning{true};
    std::condition_variable gShutdownCv;
    std::mutex gShutdownMutex;

    void handleSignal(int) {
        gRunning.store(false);
        gShutdownCv.notify_all();
    }
}

void startUSBWorkers(boost::asio::io_service& ioService, libusb_context* usbContext, ThreadPool& threadPool)
{
    auto usbWorker = [&ioService, usbContext]() {
        timeval libusbEventTimeout{180, 0};

        while(!ioService.stopped())
        {
            libusb_handle_events_timeout_completed(usbContext, &libusbEventTimeout, nullptr);
        }
    };

    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
}

void startIOServiceWorkers(boost::asio::io_service& ioService, ThreadPool& threadPool)
{
    auto ioServiceWorker = [&ioService]() {
        ioService.run();
    };

    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
}

void configureLogging() {
    const std::string logIni = "openauto-logs.ini";
    std::ifstream logSettings(logIni);
    if (logSettings.good()) {
        try {
            // For boost < 1.71 the severity types are not automatically parsed so lets register them.
            boost::log::register_simple_filter_factory<boost::log::trivial::severity_level>("Severity");
            boost::log::register_simple_formatter_factory<boost::log::trivial::severity_level, char>("Severity");
            boost::log::init_from_stream(logSettings);
        } catch (std::exception const & e) {
            OPENAUTO_LOG(warning) << "[OpenAuto] " << logIni << " was provided but was not valid.";
        }
    }
}

int main(int argc, char* argv[])
{
    configureLogging();

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    libusb_context* usbContext;
    if(libusb_init(&usbContext) != 0)
    {
        OPENAUTO_LOG(error) << "[AutoApp] libusb_init failed.";
        return 1;
    }

    boost::asio::io_service ioService;
    boost::asio::io_service::work work(ioService);
    std::vector<std::thread> threadPool;
    startUSBWorkers(ioService, usbContext, threadPool);
    startIOServiceWorkers(ioService, threadPool);

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();

    std::unique_ptr<f1x::openauto::btservice::BluetoothHandler> bluetoothHandler;
    if (configuration->getWirelessProjectionEnabled()) {
        try {
            auto androidBluetoothServer = std::make_shared<f1x::openauto::btservice::BluezBluetoothServer>(configuration);
            bluetoothHandler = std::make_unique<f1x::openauto::btservice::BluetoothHandler>(
                androidBluetoothServer,
                configuration);
        } catch (const std::runtime_error& e) {
            OPENAUTO_LOG(error) << "[AutoApp] Bluetooth service init failed: " << e.what();
        }
    }

    autoapp::projection::IBluetoothDevice::Pointer bluetoothDevice;
    if (configuration->getBluetoothAdapterAddress().empty()) {
        OPENAUTO_LOG(debug) << "[AutoApp] Using Dummy Bluetooth";
        bluetoothDevice = std::make_shared<autoapp::projection::DummyBluetoothDevice>();
    } else {
        OPENAUTO_LOG(info) << "[AutoApp] Using Local Bluetooth Adapter";
        bluetoothDevice = std::make_shared<autoapp::projection::BluezBluetoothDevice>(
            configuration->getBluetoothAdapterAddress());
    }

    autoapp::configuration::RecentAddressesList recentAddressesList(7);
    recentAddressesList.read();

    aasdk::tcp::TCPWrapper tcpWrapper;

    aasdk::usb::USBWrapper usbWrapper(usbContext);
    aasdk::usb::AccessoryModeQueryFactory queryFactory(usbWrapper, ioService);
    aasdk::usb::AccessoryModeQueryChainFactory queryChainFactory(usbWrapper, ioService, queryFactory);
    autoapp::service::ServiceFactory serviceFactory(ioService, configuration);

    auto transport = serviceFactory.getTransport();
    if (transport && !transport->isRunning()) {
        if (!transport->startAsA(std::chrono::microseconds{1000}, false)) {
            OPENAUTO_LOG(error) << "[AutoApp] Failed to start OpenAutoTransport at startup.";
        } else {
            OPENAUTO_LOG(info) << "[AutoApp] OpenAutoTransport started at startup (side A).";
        }
    }
    aasdk::messenger::interceptor::setVideoTransport(transport);

    auto& bluetoothHandlers = aasdk::messenger::interceptor::getBluetoothHandlers();
    bluetoothHandlers.setIsPairedCallback([bluetoothDevice](const std::string& address) {
        if (bluetoothDevice == nullptr) {
            return false;
        }
        return bluetoothDevice->isPaired(address);
    });

    if (transport) {
        auto& touchHandlers = aasdk::messenger::interceptor::getInputSourceHandlers();
        transport->addTypeHandler(
            buzz::wire::MsgType::TOUCH,
            [&touchHandlers](uint64_t timestamp, const void* data, std::size_t size) {
                touchHandlers.onTouchEvent(timestamp, data, size);
            });

        auto& sensorHandlers = aasdk::messenger::interceptor::getSensorHandlers();
        transport->addTypeHandler(
            buzz::wire::MsgType::SENSOR,
            [&sensorHandlers](uint64_t timestamp, const void* data, std::size_t size) {
                sensorHandlers.onSensorEvent(timestamp, data, size);
            });

        auto& mediaSourceHandlers = aasdk::messenger::interceptor::getMediaSourceHandlers();
        transport->addTypeHandler(
            buzz::wire::MsgType::MICROPHONE_AUDIO,
            [&mediaSourceHandlers](uint64_t timestamp, const void* data, std::size_t size) {
                mediaSourceHandlers.onMicrophoneAudio(timestamp, data, size);
            });
    }

    autoapp::service::AndroidAutoEntityFactory androidAutoEntityFactory(ioService, configuration, serviceFactory);

    auto usbHub(std::make_shared<aasdk::usb::USBHub>(usbWrapper, ioService, queryChainFactory));
    auto connectedAccessoriesEnumerator(std::make_shared<aasdk::usb::ConnectedAccessoriesEnumerator>(usbWrapper, ioService, queryChainFactory));
    auto app = std::make_shared<autoapp::App>(ioService, usbWrapper, tcpWrapper, androidAutoEntityFactory, std::move(usbHub), std::move(connectedAccessoriesEnumerator));

    app->waitForUSBDevice();

    {
        std::unique_lock<std::mutex> lock(gShutdownMutex);
        gShutdownCv.wait(lock, [] { return !gRunning.load(); });
    }

    ioService.stop();

    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    libusb_exit(usbContext);
    return 0;
}
