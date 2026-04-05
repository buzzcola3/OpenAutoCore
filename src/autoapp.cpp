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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <boost/asio/signal_set.hpp>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <boost/log/utility/setup.hpp>
#include <App.hpp>
#include <Messenger/MessageInStreamInterceptor.hpp>
#include <Lite/BluetoothHandler.hpp>
#include <Lite/MediaSourceHandler.hpp>
#include <Lite/InputSourceHandler.hpp>
#include <Lite/SensorHandler.hpp>
#include <nlohmann/json.hpp>
#include <open_auto_transport/wire.hpp>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>
#include <Configuration/IConfiguration.hpp>
#include <Configuration/ServiceConfig.hpp>
#include <Configuration/RecentAddressesList.hpp>
#include <Projection/IBluetoothDevice.hpp>
#include <Projection/BluezBluetoothDevice.hpp>
#include <Projection/DummyBluetoothDevice.hpp>
#include <Service/AndroidAutoEntityFactory.hpp>
#include <Service/ServiceFactory.hpp>
#include <Configuration/Configuration.hpp>
#include <Common/Log.hpp>
#include <Common/EllMainLoop.hpp>
#include <DeviceManager/DeviceManager.hpp>
#include <DeviceManager/DmLog.hpp>

namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;
using json = nlohmann::json;

namespace {
    std::atomic_bool gRunning{true};
    std::condition_variable gShutdownCv;
    std::mutex gShutdownMutex;

    void handleShutdown() {
        gRunning.store(false);
        gShutdownCv.notify_all();
    }

    autoapp::projection::IBluetoothDevice::Pointer createBluetoothDevice(
        const autoapp::configuration::IConfiguration::Pointer& configuration) {
        if (configuration == nullptr || configuration->getBluetoothAdapterAddress().empty()) {
            OPENAUTO_LOG(debug) << "[AutoApp] Using Dummy Bluetooth";
            return std::make_shared<autoapp::projection::DummyBluetoothDevice>();
        }

        OPENAUTO_LOG(info) << "[AutoApp] Using Local Bluetooth Adapter";
        return std::make_shared<autoapp::projection::BluezBluetoothDevice>(
            configuration->getBluetoothAdapterAddress());
    }
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

    // Bridge DeviceManager logs into the OpenAuto logging system
    dm_set_log_callback([](DmLogLevel level, const std::string& msg) {
        switch (level) {
            case DmLogLevel::info:    OPENAUTO_LOG(info) << "[DeviceManager] " << msg; break;
            case DmLogLevel::warning: OPENAUTO_LOG(warning) << "[DeviceManager] " << msg; break;
            case DmLogLevel::error:   OPENAUTO_LOG(error) << "[DeviceManager] " << msg; break;
        }
    });

    // Ensure ELL main loop is running (needed by DeviceManager for D-Bus + fd watches)
    f1x::openauto::common::EllMainLoop::instance().ensureRunning();

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();

    boost::asio::io_service ioService;
    boost::asio::io_service::work work(ioService);
    std::vector<std::thread> threadPool;
    startIOServiceWorkers(ioService, threadPool);

    // DeviceManager — unified USB/WiFi/BT device discovery and transport creation.
    DeviceManagerConfig dmConfig;
    dmConfig.bluetoothEnabled = configuration->getWirelessProjectionEnabled();
    dmConfig.bluetoothAdapterAddress = configuration->getBluetoothAdapterAddress();
    dmConfig.wifiInterface = configuration->getBluetoothWifiInterface();

    auto ssid = configuration->getParamFromFile("/etc/hostapd/hostapd.conf", "ssid");
    if (ssid.empty()) ssid = configuration->getParamFromFile("wifi_credentials.ini", "ssid");
    if (!ssid.empty()) dmConfig.wifiSSID = ssid;

    auto pass = configuration->getParamFromFile("/etc/hostapd/hostapd.conf", "wpa_passphrase");
    if (pass.empty()) pass = configuration->getParamFromFile("wifi_credentials.ini", "wpa_passphrase");
    if (!pass.empty()) dmConfig.wifiPassword = pass;

    DeviceManager deviceManager(dmConfig);

    autoapp::projection::IBluetoothDevice::Pointer bluetoothDevice =
        createBluetoothDevice(configuration);

    autoapp::configuration::RecentAddressesList recentAddressesList(7);
    recentAddressesList.read();

    autoapp::service::ServiceFactory serviceFactory(ioService, configuration);

    autoapp::configuration::ServiceConfig serviceConfig(
        "configuration/ServiceDiscoveryResponse.default.json",
        "configuration/UserServiceDiscoveryResponse.json");
    serviceConfig.load();

    auto transport = serviceFactory.getTransport();
    if (transport && !transport->isRunning()) {
        if (!transport->startAsA(std::chrono::microseconds{1000}, false)) {
            OPENAUTO_LOG(error) << "[AutoApp] Failed to start OpenAutoTransport at startup.";
        } else {
            OPENAUTO_LOG(info) << "[AutoApp] OpenAutoTransport started at startup (side A).";
        }
    }
    aasdk::messenger::interceptor::setVideoTransport(transport);

    auto& btLiteHandler = aasdk::messenger::interceptor::getBluetoothHandler();
    btLiteHandler.setIsPairedCallback([bluetoothDevice](const std::string& address) {
        if (bluetoothDevice == nullptr) {
            return false;
        }
        return bluetoothDevice->isPaired(address);
    });

    if (transport) {
        auto& touchHandler = aasdk::messenger::interceptor::getInputSourceHandler();
        transport->addTypeHandler(
            buzz::wire::MsgType::TOUCH,
            [&touchHandler](uint64_t timestamp, const void* data, std::size_t size) {
                touchHandler.onTouchEvent(timestamp, data, size);
            });

        auto& sensorHandler = aasdk::messenger::interceptor::getSensorHandler();
        transport->addTypeHandler(
            buzz::wire::MsgType::SENSOR,
            [&sensorHandler](uint64_t timestamp, const void* data, std::size_t size) {
                sensorHandler.onSensorEvent(timestamp, data, size);
            });

        auto& mediaSourceHandler = aasdk::messenger::interceptor::getMediaSourceHandler();
        transport->addTypeHandler(
            buzz::wire::MsgType::MICROPHONE_AUDIO,
            [&mediaSourceHandler](uint64_t timestamp, const void* data, std::size_t size) {
                mediaSourceHandler.onMicrophoneAudio(timestamp, data, size);
            });

        transport->addTypeHandler(
            buzz::wire::MsgType::CONFIGURATION,
            [&serviceConfig, transport](uint64_t, const void* data, std::size_t size) {
                auto req = nlohmann::json::parse(
                    static_cast<const char*>(data),
                    static_cast<const char*>(data) + size,
                    nullptr, false);
                if (req.is_discarded()) return;

                auto action = req.value("action", "");
                if (action == "get") {
                    auto cfg = serviceConfig.getJson();
                    transport->send(buzz::wire::MsgType::CONFIGURATION, 0,
                                    cfg.data(), cfg.size());
                } else if (action == "set") {
                    if (req.contains("config")) {
                        auto err = serviceConfig.setJson(req["config"].dump());
                        if (err.empty()) {
                            serviceConfig.save();
                        }
                    }
                } else if (action == "reset") {
                    serviceConfig.reset();
                    serviceConfig.save();
                }
            });
    }

    autoapp::service::AndroidAutoEntityFactory androidAutoEntityFactory(ioService, configuration,
                                                                        serviceConfig, serviceFactory);

    auto app = std::make_shared<autoapp::App>(ioService, androidAutoEntityFactory);

    // ── DeviceManager callbacks ──
    // DeviceManager creates transports internally; autoapp just forwards them to App.
    deviceManager.onDeviceReady = [app](const std::string& deviceId,
                                        DeviceConnection::Pointer connection) {
        OPENAUTO_LOG(info) << "[AutoApp] Device ready: " << deviceId;
        app->start(std::move(connection));
    };
    deviceManager.onDeviceListChanged = [transport, &deviceManager]() {
        if (!transport || !transport->isRunning()) return;
        std::string s = "{\"action\":\"device_list\",\"devices\":" + deviceManager.getDeviceListJson() + "}";
        transport->send(buzz::wire::MsgType::CONTROL, 0, s.data(), s.size());
    };

    // Device management via MsgType::CONTROL (JSON RPC)
    transport->addTypeHandler(
        buzz::wire::MsgType::CONTROL,
        [transport, app, &deviceManager](uint64_t, const void* data, std::size_t size) {
            auto req = json::parse(
                static_cast<const char*>(data),
                static_cast<const char*>(data) + size,
                nullptr, false);
            if (req.is_discarded()) return;

            auto action = req.value("action", "");
            if (action == "get_devices" || action == "scan_devices") {
                std::string s = "{\"action\":\"device_list\",\"devices\":" + deviceManager.getDeviceListJson() + "}";
                transport->send(buzz::wire::MsgType::CONTROL, 0, s.data(), s.size());
            } else if (action == "connect_device") {
                auto deviceId = req.value("id", "");
                if (deviceId.empty()) return;
                app->stop();
                deviceManager.connectDevice(deviceId);
            } else if (action == "disconnect_device") {
                auto deviceId = req.value("id", "");
                if (deviceId.empty()) return;
                app->stop();
                deviceManager.disconnectDevice(deviceId);
            }
        });

    boost::asio::signal_set signals(ioService, SIGINT, SIGTERM);
    signals.async_wait([app, &ioService](const boost::system::error_code& error, int) {
        if (error) {
            return;
        }
        app->stop();
        ioService.stop();
        handleShutdown();
    });

    deviceManager.start();

    {
        std::unique_lock<std::mutex> lock(gShutdownMutex);
        gShutdownCv.wait(lock, [] { return !gRunning.load(); });
    }

    deviceManager.stop();
    ioService.stop();

    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    return 0;
}
