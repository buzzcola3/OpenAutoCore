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
#include <boost/asio/steady_timer.hpp>
#include <fstream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <TCP/TCPWrapper.hpp>
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

    // ── Available-device registry (populated by DeviceManager callbacks) ──
    struct AvailableDevice {
        std::string id;
        std::string displayName;
        std::string transport;   // "usb" | "usb_raw" | "bt" | "wifi"
        std::string status = "available";  // "available" | "connecting" | "connected"
        // USB-specific
        uint16_t vid = 0;
        uint16_t pid = 0;
        uint8_t bus = 0;
        uint8_t port = 0;
        // WiFi-specific
        int fd = -1;
        std::string peerAddress;
    };

    std::mutex gDevicesMutex;
    std::vector<AvailableDevice> gAvailableDevices;
    std::atomic_bool gPendingUSBAutoConnect{false};
    std::atomic_bool gPendingWifiAutoConnect{false};

    json devicesJson() {
        json arr = json::array();
        std::lock_guard<std::mutex> lock(gDevicesMutex);
        for (auto& d : gAvailableDevices) {
            arr.push_back({
                {"id",          d.id},
                {"displayName", d.displayName},
                {"transport",   d.transport},
                {"status",      d.status}
            });
        }
        return arr;
    }

    void broadcastDeviceList(const std::shared_ptr<buzz::autoapp::Transport::Transport>& tp) {
        if (!tp || !tp->isRunning()) return;
        json msg = {{"action", "device_list"}, {"devices", devicesJson()}};
        auto s = msg.dump();
        tp->send(buzz::wire::MsgType::CONTROL, 0, s.data(), s.size());
    }

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

    // DeviceManager — unified USB/WiFi/BT device discovery (ELL-native)
    // Created early because it owns the libusb context used by all USB objects.
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
    libusb_context* usbContext = deviceManager.usbContext();
    if (!usbContext) {
        OPENAUTO_LOG(error) << "[AutoApp] DeviceManager failed to create libusb context.";
        return 1;
    }

    boost::asio::io_service ioService;
    boost::asio::io_service::work work(ioService);
    std::vector<std::thread> threadPool;
    startUSBWorkers(ioService, usbContext, threadPool);
    startIOServiceWorkers(ioService, threadPool);

    autoapp::projection::IBluetoothDevice::Pointer bluetoothDevice =
        createBluetoothDevice(configuration);

    autoapp::configuration::RecentAddressesList recentAddressesList(7);
    recentAddressesList.read();

    aasdk::tcp::TCPWrapper tcpWrapper;
    aasdk::usb::USBWrapper usbWrapper(usbContext);
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

    auto app = std::make_shared<autoapp::App>(ioService, usbWrapper, tcpWrapper, androidAutoEntityFactory);

    // Device management via MsgType::CONTROL (JSON RPC) — must be after app creation
    transport->addTypeHandler(
        buzz::wire::MsgType::CONTROL,
        [transport, &usbWrapper, &ioService, app, &deviceManager](uint64_t, const void* data, std::size_t size) {
            auto req = json::parse(
                static_cast<const char*>(data),
                static_cast<const char*>(data) + size,
                nullptr, false);
            if (req.is_discarded()) return;

            auto action = req.value("action", "");
            if (action == "get_devices") {
                json msg = {{"action", "device_list"}, {"devices", devicesJson()}};
                auto s = msg.dump();
                transport->send(buzz::wire::MsgType::CONTROL, 0, s.data(), s.size());

            } else if (action == "connect_device") {
                auto deviceId = req.value("id", "");
                if (deviceId.empty()) return;

                AvailableDevice target;
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(gDevicesMutex);
                    for (auto& d : gAvailableDevices) {
                        if (d.id == deviceId) { target = d; found = true; break; }
                    }
                }
                if (!found) {
                    OPENAUTO_LOG(warning) << "[AutoApp] connect_device: unknown id " << deviceId;
                    return;
                }

                if (target.transport == "usb") {
                    // AOAP device — open and connect directly
                    app->stop();

                    auto handle = usbWrapper.openDeviceWithVidPid(target.vid, target.pid);
                    if (!handle) {
                        OPENAUTO_LOG(error) << "[AutoApp] Failed to open USB device " << deviceId;
                        return;
                    }
                    OPENAUTO_LOG(info) << "[AutoApp] Connecting USB device " << deviceId;
                    app->startUSBDevice(std::move(handle));
                } else if (target.transport == "usb_raw") {
                    // Non-AOAP phone — start AOAP setup, auto-connect when it re-enumerates
                    app->stop();
                    gPendingUSBAutoConnect.store(true);
                    OPENAUTO_LOG(info) << "[AutoApp] Starting AOAP setup for " << deviceId;
                    deviceManager.beginAoapSetup(deviceId);
                    {
                        std::lock_guard<std::mutex> lock(gDevicesMutex);
                        for (auto& d : gAvailableDevices) {
                            if (d.id == deviceId) { d.status = "connecting"; break; }
                        }
                    }
                    broadcastDeviceList(transport);
                    return;
                } else if (target.transport == "bt") {
                    // BT device — start WiFi projection handshake, auto-connect when WiFi arrives
                    app->stop();
                    gPendingWifiAutoConnect.store(true);
                    OPENAUTO_LOG(info) << "[AutoApp] Starting WiFi projection for " << deviceId;
                    deviceManager.beginWifiProjection();
                    {
                        std::lock_guard<std::mutex> lock(gDevicesMutex);
                        for (auto& d : gAvailableDevices) {
                            if (d.id == deviceId) { d.status = "connecting"; break; }
                        }
                    }
                    broadcastDeviceList(transport);
                    return;
                } else if (target.transport == "wifi") {
                    if (target.fd < 0) {
                        OPENAUTO_LOG(error) << "[AutoApp] WiFi device " << deviceId << " has no valid fd";
                        return;
                    }
                    app->stop();

                    // Consume the fd from the device list so it can't be reused
                    {
                        std::lock_guard<std::mutex> lock(gDevicesMutex);
                        for (auto& d : gAvailableDevices) {
                            if (d.id == deviceId) { d.fd = -1; break; }
                        }
                    }

                    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
                    boost::system::error_code ec;
                    socket->assign(boost::asio::ip::tcp::v4(), target.fd, ec);
                    if (ec) {
                        OPENAUTO_LOG(error) << "[AutoApp] Failed to assign WiFi fd: " << ec.message();
                        ::close(target.fd);
                        return;
                    }
                    OPENAUTO_LOG(info) << "[AutoApp] Connecting WiFi device " << deviceId;
                    app->start(std::move(socket));
                }

                // Mark as connected (keep in list so FE can see it)
                {
                    std::lock_guard<std::mutex> lock(gDevicesMutex);
                    for (auto& d : gAvailableDevices) {
                        if (d.id == deviceId) { d.status = "connected"; break; }
                    }
                }
                broadcastDeviceList(transport);

            } else if (action == "disconnect_device") {
                auto deviceId = req.value("id", "");
                if (deviceId.empty()) return;

                OPENAUTO_LOG(info) << "[AutoApp] Disconnecting device " << deviceId;

                // Check if this is a WiFi or USB device (for post-disconnect actions)
                bool wasWifi = false;
                bool wasUSB = false;
                uint16_t usbVid = 0, usbPid = 0;
                {
                    std::lock_guard<std::mutex> lock(gDevicesMutex);
                    for (auto& d : gAvailableDevices) {
                        if (d.id == deviceId) {
                            wasWifi = (d.transport == "wifi");
                            wasUSB = (d.transport == "usb");
                            usbVid = d.vid;
                            usbPid = d.pid;
                            break;
                        }
                    }
                }

                app->stop();

                // Remove the stale entry — the device will re-appear via
                // hotplug (USB) or TCP accept (WiFi) with fresh connection info.
                {
                    std::lock_guard<std::mutex> lock(gDevicesMutex);
                    gAvailableDevices.erase(
                        std::remove_if(gAvailableDevices.begin(), gAvailableDevices.end(),
                                       [&deviceId](const AvailableDevice& d) { return d.id == deviceId; }),
                        gAvailableDevices.end());
                }
                broadcastDeviceList(transport);

                // For WiFi devices: cycle BT connection so phone can reconnect wirelessly
                if (wasWifi) {
                    deviceManager.reconnectBluetooth();
                }
                // For USB devices: reset device to exit AOAP mode.
                // The phone re-enumerates as its normal VID/PID, hotplug fires,
                // and it reappears as a usb_raw phone ready to connect again.
                if (wasUSB && usbVid && usbPid) {
                    auto resetTimer = std::make_shared<boost::asio::steady_timer>(ioService, std::chrono::milliseconds(200));
                    resetTimer->async_wait([resetTimer, &usbWrapper, usbVid, usbPid](const boost::system::error_code& ec) {
                        if (ec) return;
                        auto handle = usbWrapper.openDeviceWithVidPid(usbVid, usbPid);
                        if (handle) {
                            int rc = libusb_reset_device(handle.get());
                            OPENAUTO_LOG(info) << "[AutoApp] USB reset to exit AOAP: "
                                              << libusb_strerror(static_cast<libusb_error>(rc));
                        } else {
                            OPENAUTO_LOG(warning) << "[AutoApp] Could not open AOAP device for reset";
                        }
                    });
                }
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

    // DeviceManager callbacks — register available devices & broadcast to FE
    deviceManager.onUSBDeviceAvailable = [transport, &usbWrapper, &ioService, app](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
        std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);
        OPENAUTO_LOG(info) << "[DeviceManager] AOAP device available: " << id
                          << " " << std::hex << vid << ":" << pid << std::dec;

        if (gPendingUSBAutoConnect.exchange(false)) {
            // User already clicked connect — delay 500ms for udev to set permissions
            OPENAUTO_LOG(info) << "[AutoApp] Auto-connecting AOAP device " << id << " (waiting for udev)";
            auto timer = std::make_shared<boost::asio::steady_timer>(ioService, std::chrono::milliseconds(500));
            timer->async_wait([timer, transport, &usbWrapper, app, id, bus, port, vid, pid](const boost::system::error_code& ec) {
                if (ec) return;
                auto handle = usbWrapper.openDeviceWithVidPid(vid, pid);
                if (handle) {
                    {
                        std::lock_guard<std::mutex> lock(gDevicesMutex);
                        gAvailableDevices.erase(
                            std::remove_if(gAvailableDevices.begin(), gAvailableDevices.end(),
                                           [](const AvailableDevice& d) {
                                               return d.transport == "usb_raw" || d.transport == "usb";
                                           }),
                            gAvailableDevices.end());
                        AvailableDevice dev;
                        dev.id = id;
                        dev.displayName = "Android Auto (USB)";
                        dev.transport = "usb";
                        dev.status = "connected";
                        dev.vid = vid;
                        dev.pid = pid;
                        dev.bus = bus;
                        dev.port = port;
                        gAvailableDevices.push_back(std::move(dev));
                    }
                    app->startUSBDevice(std::move(handle));
                    broadcastDeviceList(transport);
                } else {
                    OPENAUTO_LOG(error) << "[AutoApp] Failed to open auto-connect AOAP device";
                    gPendingUSBAutoConnect.store(true); // retry on next enumeration
                }
            });
            return;
        }

        // Normal listing (phone was already in AOAP mode)
        AvailableDevice dev;
        dev.id = id;
        dev.displayName = "Android Auto (USB)";
        dev.transport = "usb";
        dev.vid = vid;
        dev.pid = pid;
        dev.bus = bus;
        dev.port = port;
        {
            std::lock_guard<std::mutex> lock(gDevicesMutex);
            auto it = std::find_if(gAvailableDevices.begin(), gAvailableDevices.end(),
                                   [&id](const AvailableDevice& d) { return d.id == id; });
            if (it != gAvailableDevices.end()) {
                it->vid = vid;
                it->pid = pid;
            } else {
                // Remove any stale (non-connected) USB entry before adding
                gAvailableDevices.erase(
                    std::remove_if(gAvailableDevices.begin(), gAvailableDevices.end(),
                                   [](const AvailableDevice& d) {
                                       return d.transport == "usb" && d.status != "connected";
                                   }),
                    gAvailableDevices.end());
                gAvailableDevices.push_back(std::move(dev));
            }
        }
        broadcastDeviceList(transport);
    };
    deviceManager.onWifiClientConnected = [transport, &ioService, app](const std::string& deviceId, int fd, const std::string& peerAddress) {
        OPENAUTO_LOG(info) << "[DeviceManager] WiFi client available: " << deviceId << " fd=" << fd;

        if (gPendingWifiAutoConnect.exchange(false)) {
            // User already clicked connect on BT device — auto-connect this WiFi client
            OPENAUTO_LOG(info) << "[AutoApp] Auto-connecting WiFi device " << deviceId;
            {
                std::lock_guard<std::mutex> lock(gDevicesMutex);
                // Update existing entry from bt → wifi
                for (auto& d : gAvailableDevices) {
                    if (d.id == deviceId) {
                        d.transport = "wifi";
                        d.status = "connected";
                        d.fd = -1;
                        d.peerAddress = peerAddress;
                        d.displayName = "Android Auto (WiFi - " + peerAddress + ")";
                        break;
                    }
                }
            }
            auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);
            boost::system::error_code ec;
            socket->assign(boost::asio::ip::tcp::v4(), fd, ec);
            if (ec) {
                OPENAUTO_LOG(error) << "[AutoApp] Failed to assign auto-connect WiFi fd: " << ec.message();
                ::close(fd);
            } else {
                app->start(std::move(socket));
            }
            broadcastDeviceList(transport);
            return;
        }

        // Normal listing — update existing entry or add new
        {
            std::lock_guard<std::mutex> lock(gDevicesMutex);
            bool found = false;
            for (auto& d : gAvailableDevices) {
                if (d.id == deviceId) {
                    d.transport = "wifi";
                    d.fd = fd;
                    d.peerAddress = peerAddress;
                    d.displayName = "Android Auto (WiFi - " + peerAddress + ")";
                    found = true;
                    break;
                }
            }
            if (!found) {
                AvailableDevice dev;
                dev.id = deviceId;
                dev.displayName = "Android Auto (WiFi - " + peerAddress + ")";
                dev.transport = "wifi";
                dev.fd = fd;
                dev.peerAddress = peerAddress;
                gAvailableDevices.push_back(std::move(dev));
            }
        }
        broadcastDeviceList(transport);
    };
    deviceManager.onUSBPhoneDetected = [transport](uint8_t bus, uint8_t port, uint16_t vid, uint16_t pid) {
        std::string id = "usb:" + std::to_string(bus) + ":" + std::to_string(port);
        OPENAUTO_LOG(info) << "[DeviceManager] USB phone detected: " << id
                          << " " << std::hex << vid << ":" << pid << std::dec;

        AvailableDevice dev;
        dev.id = id;
        dev.displayName = "Android Phone (USB)";
        dev.transport = "usb_raw";
        dev.vid = vid;
        dev.pid = pid;
        dev.bus = bus;
        dev.port = port;
        {
            std::lock_guard<std::mutex> lock(gDevicesMutex);
            gAvailableDevices.erase(
                std::remove_if(gAvailableDevices.begin(), gAvailableDevices.end(),
                               [&id](const AvailableDevice& d) { return d.id == id; }),
                gAvailableDevices.end());
            gAvailableDevices.push_back(std::move(dev));
        }
        broadcastDeviceList(transport);
    };
    deviceManager.onBtDeviceAvailable = [transport](const std::string& deviceId, const std::string& btAddress) {
        OPENAUTO_LOG(info) << "[DeviceManager] BT device available: " << deviceId;

        AvailableDevice dev;
        dev.id = deviceId;
        dev.displayName = "Android Auto (BT - " + btAddress + ")";
        dev.transport = "bt";
        {
            std::lock_guard<std::mutex> lock(gDevicesMutex);
            // Remove any existing BT entry (only one BT device at a time)
            gAvailableDevices.erase(
                std::remove_if(gAvailableDevices.begin(), gAvailableDevices.end(),
                               [](const AvailableDevice& d) { return d.transport == "bt"; }),
                gAvailableDevices.end());
            gAvailableDevices.push_back(std::move(dev));
        }
        broadcastDeviceList(transport);
    };
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
