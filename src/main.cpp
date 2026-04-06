/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*  Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
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
#include <fstream>
#include <thread>
#include <unistd.h>
#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/log/utility/setup.hpp>
#include <nlohmann/json.hpp>
#include <open_auto_transport/wire.hpp>
#include <open_auto_transport/transport.hpp>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>

#include <Transport/SSLWrapper.hpp>
#include <Common/Cryptor.hpp>
#include <Common/ICryptor.hpp>
#include <FrameRouter/FrameRouter.hpp>
#include <Lite/ControlHandler.hpp>
#include <Lite/BluetoothHandler.hpp>
#include <Lite/MediaSourceHandler.hpp>
#include <Lite/InputSourceHandler.hpp>
#include <Lite/SensorHandler.hpp>
#include <Error/Error.hpp>

#include <Configuration/IConfiguration.hpp>
#include <Configuration/ServiceConfig.hpp>
#include <Configuration/RecentAddressesList.hpp>
#include <Configuration/Configuration.hpp>
#include <Projection/IBluetoothDevice.hpp>
#include <Projection/BluezBluetoothDevice.hpp>
#include <Projection/DummyBluetoothDevice.hpp>
#include <DeviceManager/Common/DeviceConnection.hpp>
#include <DeviceManager/Common/DeviceManager.hpp>
#include <DeviceManager/Common/DmLog.hpp>
#include <Common/Log.hpp>
#include <Common/EllMainLoop.hpp>

namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;
using json = nlohmann::json;

// ── App — session lifecycle ──

class App : public std::enable_shared_from_this<App> {
public:
    using Pointer = std::shared_ptr<App>;

    App(boost::asio::io_service& ioService,
        autoapp::configuration::ServiceConfig& serviceConfig,
        std::shared_ptr<buzz::autoapp::Transport::Transport> transport)
        : ioService_(ioService), strand_(ioService_),
          serviceConfig_(serviceConfig), transport_(std::move(transport)) {}

    void start(DeviceConnection::Pointer connection) {
        strand_.dispatch([this, self = shared_from_this(), conn = std::move(connection)]() mutable {
            OPENAUTO_LOG(info) << "[App] Device connected.";

            if (router_) {
                OPENAUTO_LOG(warning) << "[App] Session still running, stopping first.";
                router_->controlHandler().teardownSession();
                router_->stop();
                cryptor_->deinit();
                router_.reset();
                cryptor_.reset();
            }

            try {
                auto sslWrapper = std::make_shared<aasdk::transport::SSLWrapper>();
                cryptor_ = std::make_shared<aasdk::messenger::Cryptor>(std::move(sslWrapper));
                cryptor_->init();

                router_ = std::make_shared<aasdk::FrameRouter>(std::move(conn), cryptor_, transport_);

                if (onSessionStarted_) onSessionStarted_(*router_);

                auto& ctrl = router_->controlHandler();
                ctrl.initSession(*cryptor_, serviceConfig_, ioService_,
                                 [this, self]() { onSessionEnd(); });

                router_->start();
                ctrl.sendVersionRequest();
            } catch (const aasdk::error::Error& error) {
                OPENAUTO_LOG(error) << "[App] Session create error: " << error.what();
                router_.reset();
                cryptor_.reset();
            }
        });
    }

    void stop() {
        strand_.dispatch([this, self = shared_from_this()]() {
            if (!router_) return;
            OPENAUTO_LOG(info) << "[App] stop()";

            router_->controlHandler().teardownSession();
            router_->stop();
            cryptor_->deinit();
            router_.reset();
            cryptor_.reset();
        });
    }

    /// Access the current router (may be null between sessions).
    aasdk::FrameRouter::Pointer router() const { return router_; }

    /// Callback invoked right after a new router/session is created.
    void setOnSessionStarted(std::function<void(aasdk::FrameRouter&)> cb) {
        onSessionStarted_ = std::move(cb);
    }

private:
    void onSessionEnd() {
        strand_.dispatch([this, self = shared_from_this()]() {
            OPENAUTO_LOG(info) << "[App] Session ended.";

            if (router_) {
                router_->controlHandler().teardownSession();
                router_->stop();
                cryptor_->deinit();
                router_.reset();
                cryptor_.reset();
            }
        });
    }

    boost::asio::io_service& ioService_;
    boost::asio::io_service::strand strand_;
    autoapp::configuration::ServiceConfig& serviceConfig_;
    std::shared_ptr<buzz::autoapp::Transport::Transport> transport_;
    aasdk::messenger::ICryptor::Pointer cryptor_;
    aasdk::FrameRouter::Pointer router_;
    std::function<void(aasdk::FrameRouter&)> onSessionStarted_;
};

// ── Helpers ──

namespace {

std::atomic_bool gRunning{true};

void handleShutdown() {
    gRunning.store(false);
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

void startIOServiceWorkers(boost::asio::io_service& ioService, ThreadPool& threadPool) {
    auto ioServiceWorker = [&ioService]() { ioService.run(); };
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
            boost::log::register_simple_filter_factory<boost::log::trivial::severity_level>("Severity");
            boost::log::register_simple_formatter_factory<boost::log::trivial::severity_level, char>("Severity");
            boost::log::init_from_stream(logSettings);
        } catch (std::exception const& e) {
            OPENAUTO_LOG(warning) << "[OpenAuto] " << logIni << " was provided but was not valid.";
        }
    }
}

} // namespace

// ── Main ──

int main(int argc, char* argv[])
{
    configureLogging();

    dm_set_log_callback([](DmLogLevel level, const std::string& msg) {
        switch (level) {
            case DmLogLevel::info:    OPENAUTO_LOG(info) << "[DeviceManager] " << msg; break;
            case DmLogLevel::warning: OPENAUTO_LOG(warning) << "[DeviceManager] " << msg; break;
            case DmLogLevel::error:   OPENAUTO_LOG(error) << "[DeviceManager] " << msg; break;
        }
    });

    f1x::openauto::common::EllMainLoop::instance().ensureRunning();

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();

    boost::asio::io_service ioService;
    boost::asio::io_service::work work(ioService);
    std::vector<std::thread> threadPool;
    startIOServiceWorkers(ioService, threadPool);

    // DeviceManager
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

    autoapp::configuration::ServiceConfig serviceConfig(
        "configuration/ServiceDiscoveryResponse.default.json",
        "configuration/UserServiceDiscoveryResponse.json");
    serviceConfig.load();

    auto transport = std::make_shared<buzz::autoapp::Transport::Transport>();
    if (transport && !transport->isRunning()) {
        if (!transport->startAsA(std::chrono::microseconds{1000}, false)) {
            OPENAUTO_LOG(error) << "[AutoApp] Failed to start OpenAutoTransport at startup.";
        } else {
            OPENAUTO_LOG(info) << "[AutoApp] OpenAutoTransport started at startup (side A).";
        }
    }
    auto app = std::make_shared<App>(ioService, serviceConfig, transport);

    // Bluetooth pairing check — deferred through app->router() since
    // the router (and thus handlers) are created per-session.
    auto bluetoothIsPaired = [bluetoothDevice](const std::string& address) -> bool {
        if (bluetoothDevice == nullptr) return false;
        return bluetoothDevice->isPaired(address);
    };

    if (transport) {
        // Transport type handlers dereference through app->router() at call time.
        // If no session is active (router is null), the event is silently dropped.
        transport->addTypeHandler(
            buzz::wire::MsgType::TOUCH,
            [app](uint64_t timestamp, const void* data, std::size_t size) {
                if (auto r = app->router()) r->inputSourceHandler().onTouchEvent(timestamp, data, size);
            });

        transport->addTypeHandler(
            buzz::wire::MsgType::SENSOR,
            [app](uint64_t timestamp, const void* data, std::size_t size) {
                if (auto r = app->router()) r->sensorHandler().onSensorEvent(timestamp, data, size);
            });

        transport->addTypeHandler(
            buzz::wire::MsgType::MICROPHONE_AUDIO,
            [app](uint64_t timestamp, const void* data, std::size_t size) {
                if (auto r = app->router()) r->mediaSourceHandler().onMicrophoneAudio(timestamp, data, size);
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

    // Wire bluetooth pairing callback into each new session's handler.
    app->setOnSessionStarted([bluetoothIsPaired](aasdk::FrameRouter& router) {
        router.bluetoothHandler().setIsPairedCallback(bluetoothIsPaired);
    });

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
        if (error) return;
        app->stop();
        ioService.stop();
        handleShutdown();
    });

    deviceManager.start();

    while (gRunning.load()) {
        deviceManager.execute();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    deviceManager.stop();
    ioService.stop();

    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    return 0;
}
