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

    //QApplication qApplication(argc, argv);
    //int width = QApplication::desktop()->width();
    //int height = QApplication::desktop()->height();

    //for (QScreen *screen : qApplication.screens()) {
    //  OPENAUTO_LOG(info) << "[AutoApp] Screen name: " << screen->name().toStdString();
    //  OPENAUTO_LOG(info) << "[AutoApp] Screen geometry: " << screen->geometry().width(); // This includes position and size
    //  OPENAUTO_LOG(info) << "[AutoApp] Screen physical size: " << screen->physicalSize().width(); // Size in millimeters
    //}

    //QScreen *primaryScreen = QGuiApplication::primaryScreen();

    // Check if a primary screen was found
    //if (primaryScreen) {
    //  // Get the geometry of the primary screen
    //  Rect screenGeometry = primaryScreen->geometry();
    //  width = screenGeometry.width();
    //  height = screenGeometry.height();
    //  OPENAUTO_LOG(info) << "[AutoApp] Using gemoetry from primary screen.";
    //} else {
    //  OPENAUTO_LOG(info) << "[AutoApp] Unable to find primary screen, using default values.";
    //}

    //OPENAUTO_LOG(info) << "[AutoApp] Display width: " << width;
    //OPENAUTO_LOG(info) << "[AutoApp] Display height: " << height;

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();

    //autoapp::ui::MainWindow mainWindow(configuration);

    //autoapp::ui::SettingsWindow settingsWindow(configuration);

    //settingsWindow.setFixedSize(width, height);
    //settingsWindow.adjustSize();

    //autoapp::configuration::RecentAddressesList recentAddressesList(7);
    //recentAddressesList.read();

    aasdk::tcp::TCPWrapper tcpWrapper;

    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::exit, []() { system("touch /tmp/shutdown"); std::exit(0); });
    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::reboot, []() { system("touch /tmp/reboot"); std::exit(0); });
    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::showFullScreen);
    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::show_tab2);

    //if (configuration->showCursor() == false) {
    //    qApplication.setOverrideCursor(Qt::BlankCursor);
    //} else {
    //    qApplication.setOverrideCursor(Qt::ArrowCursor);
    //}

    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraHide, [&qApplication]() {
    //    system("/opt/crankshaft/cameracontrol.py Background &");
    //    OPENAUTO_LOG(debug) << "[AutoApp] Camera Background.";
    //});

    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraShow, [&qApplication]() {
    //    system("/opt/crankshaft/cameracontrol.py Foreground &");
    //    OPENAUTO_LOG(debug) << "[AutoApp] Camera Foreground.";
    //});

    //mainWindow.showFullScreen();
    //mainWindow.setFixedSize(width, height);
    //mainWindow.adjustSize();

    aasdk::usb::USBWrapper usbWrapper(usbContext);
    aasdk::usb::AccessoryModeQueryFactory queryFactory(usbWrapper, ioContext );
    aasdk::usb::AccessoryModeQueryChainFactory queryChainFactory(usbWrapper, ioContext , queryFactory);
    autoapp::service::ServiceFactory serviceFactory(ioContext , configuration);
    autoapp::service::AndroidAutoEntityFactory androidAutoEntityFactory(ioContext , configuration, serviceFactory);

    auto usbHub(std::make_shared<aasdk::usb::USBHub>(usbWrapper, ioContext , queryChainFactory));
    auto connectedAccessoriesEnumerator(std::make_shared<aasdk::usb::ConnectedAccessoriesEnumerator>(usbWrapper, ioContext , queryChainFactory));
    auto app = std::make_shared<autoapp::App>(ioContext , usbWrapper, tcpWrapper, androidAutoEntityFactory, std::move(usbHub), std::move(connectedAccessoriesEnumerator));

    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::TriggerAppStop, [&app]() {
    //    try {
    //        if (std::ifstream("/tmp/android_device")) {
    //            OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStop: Manual stop usb android auto.";
    //            app->disableAutostartEntity = true;
    //            try {
    //                app->stop();
    //                //app->pause();
    //            } catch (...) {
    //                OPENAUTO_LOG(error) << "[AutoApp] TriggerAppStop: stop();";
    //            }

    //        } else {
    //            OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStop: Manual stop wifi android auto.";
    //            try {
    //                app->onAndroidAutoQuit();
    //                //app->pause();
    //            } catch (...) {
    //                OPENAUTO_LOG(error) << "[Autoapp] TriggerAppStop: stop();";
    //            }

    //        }
    //    } catch (...) {
    //        OPENAUTO_LOG(error) << "[AutoApp] Exception in manual stop android auto.";
    //    }
    //});

    //QObject::connect(&mainWindow, &autoapp::ui::MainWindow::CloseAllDialogs, [&settingsWindow]() {
    //    settingsWindow.close();
    //    OPENAUTO_LOG(debug) << "[AutoApp] Close all possible open dialogs.";
    //});


    app->waitForUSBDevice();

    //auto result = qApplication.exec();

    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    libusb_exit(usbContext);
    return 0;
}
