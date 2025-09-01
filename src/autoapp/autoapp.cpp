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

#include <thread>
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
#include <buzz/autoapp/Transport/transport.hpp>

namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;

using IoContext = boost::asio::io_context;
using Strand = boost::asio::strand<IoContext::executor_type>;

void startUSBWorkers(IoContext& ioContext, libusb_context* usbContext, ThreadPool& threadPool)
{
    auto usbWorker = [&ioContext, usbContext]() {
        timeval libusbEventTimeout{180, 0};

        while(!ioContext.stopped())
        {
            libusb_handle_events_timeout_completed(usbContext, &libusbEventTimeout, nullptr);
        }
    };

    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
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

    auto transport = std::make_shared<buzz::autoapp::Transport::Transport>(/*maxQueue=*/1024);

    libusb_context* usbContext;
    if(libusb_init(&usbContext) != 0)
    {
        OPENAUTO_LOG(error) << "[AutoApp] libusb_init failed.";
        return 1;
    }

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

    libusb_exit(usbContext);
    return 0;
}
