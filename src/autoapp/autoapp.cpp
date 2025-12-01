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
#include <ctime>
#include <mutex>
#include <thread>
#include <string>
#include "buzz/autoapp/DebugGlassMonitor.hpp"
#include "debugglass/debugglass.h"
#include "debugglass/widgets/structure.h"
#include "debugglass/widgets/variable.h"
#include <USB/USBHub.hpp>
#include <USB/ConnectedAccessoriesEnumerator.hpp>
#include <USB/AccessoryModeQueryChain.hpp>
#include <USB/AccessoryModeQueryChainFactory.hpp>
#include <USB/AccessoryModeQueryFactory.hpp>
#include <TCP/TCPWrapper.hpp>
#include <boost/log/utility/setup.hpp>
#include <f1x/openauto/autoapp/App.hpp>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Service/AndroidAutoEntityFactory.hpp>
#include <f1x/openauto/autoapp/Service/ServiceFactory.hpp>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/Common/Log.hpp>
#include <buzz/common/Rect.hpp>
#include "open_auto_transport/transport.hpp"

namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;

using IoContext = boost::asio::io_context;
using Strand = boost::asio::strand<IoContext::executor_type>;

namespace buzz::autoapp {
debugglass::DebugGlass gDebugGlassMonitor;
}

namespace {

constexpr std::size_t kUsbWorkerCount = 4;

class DebugGlassUsbMonitor {
public:
    void Initialize(libusb_context* context, std::size_t worker_count) {
        std::lock_guard<std::mutex> lock(mutex_);
        context_ = context;
        worker_count_ = worker_count;
    }

    void RecordEventResult(int result) {
        total_event_calls_.fetch_add(1, std::memory_order_relaxed);
        if (result < 0) {
            last_error_code_.store(result, std::memory_order_relaxed);
        }

        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        const auto now = Clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        if (now - last_publish_time_ < std::chrono::milliseconds(250)) {
            return;
        }

        if (!EnsureWidgetsLocked()) {
            return;
        }

        last_publish_time_ = now;
        total_events_var_->SetValue(total_event_calls_.load());
        last_result_var_->SetValue(result);
        last_error_var_->SetValue(last_error_code_.load());
        worker_count_var_->SetValue(worker_count_);
        devices_var_->SetValue(QueryDeviceSummaryLocked());
        last_update_var_->SetValue(FormatTimestampLocked());
    }

private:
    using Clock = std::chrono::steady_clock;

    bool EnsureWidgetsLocked() {
        if (total_events_var_ != nullptr) {
            return true;
        }

        auto& window = gDebugGlassMonitor.windows["USB"];
        auto* tab = window.tabs.find("Event Loop");
        if (tab == nullptr) {
            tab = &window.tabs.Add("Event Loop");
        }

        structure_ = &tab->AddStructure("libusb");
        total_events_var_ = &structure_->AddVariable("Total Event Calls");
        last_result_var_ = &structure_->AddVariable("Last Result");
        last_error_var_ = &structure_->AddVariable("Last Error Code");
        devices_var_ = &structure_->AddVariable("Enumerated Devices");
        worker_count_var_ = &structure_->AddVariable("Worker Threads");
        last_update_var_ = &structure_->AddVariable("Last Update");
        return true;
    }

    std::string QueryDeviceSummaryLocked() {
        if (context_ == nullptr) {
            return std::string{"n/a"};
        }

        libusb_device** list = nullptr;
        const ssize_t count = libusb_get_device_list(context_, &list);
        std::string summary;
        if (count < 0) {
            summary = std::string("error ") + std::to_string(count);
        } else {
            summary = std::to_string(count);
        }

        if (list != nullptr) {
            libusb_free_device_list(list, 1);
        }

        return summary;
    }

    std::string FormatTimestampLocked() const {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&tt, &tm);
        char buffer[32];
        if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm) == 0) {
            return "";
        }
        return buffer;
    }

    std::atomic<uint64_t> total_event_calls_{0};
    std::atomic<int> last_error_code_{0};
    libusb_context* context_ = nullptr;
    std::size_t worker_count_ = 0;
    Clock::time_point last_publish_time_{};
    mutable std::mutex mutex_;
    debugglass::Structure* structure_ = nullptr;
    debugglass::Variable* total_events_var_ = nullptr;
    debugglass::Variable* last_result_var_ = nullptr;
    debugglass::Variable* last_error_var_ = nullptr;
    debugglass::Variable* devices_var_ = nullptr;
    debugglass::Variable* worker_count_var_ = nullptr;
    debugglass::Variable* last_update_var_ = nullptr;
};

DebugGlassUsbMonitor& GetUsbMonitor() {
    static DebugGlassUsbMonitor monitor;
    return monitor;
}

}

void startUSBWorkers(IoContext& ioContext, libusb_context* usbContext, ThreadPool& threadPool)
{
    auto usbWorker = [&ioContext, usbContext]() {
        timeval libusbEventTimeout{180, 0};

        while(!ioContext.stopped())
        {
            const int result = libusb_handle_events_timeout_completed(usbContext, &libusbEventTimeout, nullptr);
            GetUsbMonitor().RecordEventResult(result);
        }
    };

    for (std::size_t i = 0; i < kUsbWorkerCount; ++i)
    {
        threadPool.emplace_back(usbWorker);
    }
}

void startIOContextWorkers(IoContext& ioContext, ThreadPool& threadPool)
{
    auto ioContextWorker = [&ioContext]() {
        ioContext.run();
    };

    threadPool.emplace_back(ioContextWorker);
    threadPool.emplace_back(ioContextWorker);
    threadPool.emplace_back(ioContextWorker);
    threadPool.emplace_back(ioContextWorker);
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

    if (!gDebugGlassMonitor.Run())
    {
        OPENAUTO_LOG(error) << "[DebugGlass] Failed to start debug UI.";
    }

    auto transport = std::make_shared<buzz::autoapp::Transport::Transport>();

    libusb_context* usbContext;
    if(libusb_init(&usbContext) != 0)
    {
        OPENAUTO_LOG(error) << "[AutoApp] libusb_init failed.";
        return 1;
    }
    GetUsbMonitor().Initialize(usbContext, kUsbWorkerCount);

    IoContext ioContext ;
    auto work_guard = boost::asio::make_work_guard(ioContext);
    std::vector<std::thread> threadPool;
    startUSBWorkers(ioContext , usbContext, threadPool);
    startIOContextWorkers(ioContext , threadPool);

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();

    //autoapp::configuration::RecentAddressesList recentAddressesList(7);
    //recentAddressesList.read();

    aasdk::tcp::TCPWrapper tcpWrapper;

    aasdk::usb::USBWrapper usbWrapper(usbContext);
    aasdk::usb::AccessoryModeQueryFactory queryFactory(usbWrapper, ioContext );
    aasdk::usb::AccessoryModeQueryChainFactory queryChainFactory(usbWrapper, ioContext , queryFactory);
    autoapp::service::ServiceFactory serviceFactory(ioContext , configuration, transport);
    autoapp::service::AndroidAutoEntityFactory androidAutoEntityFactory(ioContext , configuration, serviceFactory);

    auto usbHub(std::make_shared<aasdk::usb::USBHub>(usbWrapper, ioContext , queryChainFactory));
    auto connectedAccessoriesEnumerator(std::make_shared<aasdk::usb::ConnectedAccessoriesEnumerator>(usbWrapper, ioContext , queryChainFactory));
    auto app = std::make_shared<autoapp::App>(ioContext , usbWrapper, tcpWrapper, androidAutoEntityFactory, std::move(usbHub), std::move(connectedAccessoriesEnumerator));


    app->waitForUSBDevice();

    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    gDebugGlassMonitor.Stop();

    libusb_exit(usbContext);
    return 0;
}
