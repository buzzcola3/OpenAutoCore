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

#pragma once

#include <boost/asio.hpp>
#include <DeviceManager/DeviceConnection.hpp>
#include <Service/IAndroidAutoEntityEventHandler.hpp>
#include <Service/IAndroidAutoEntityFactory.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{

class App: public service::IAndroidAutoEntityEventHandler, public std::enable_shared_from_this<App>
{
public:
    typedef std::shared_ptr<App> Pointer;

    App(boost::asio::io_service& ioService,
        service::IAndroidAutoEntityFactory& androidAutoEntityFactory);

    void start(DeviceConnection::Pointer connection);
    void stop();
    void pause();
    void resume();
    void onAndroidAutoQuit() override;
    bool disableAutostartEntity = false;

private:
    using std::enable_shared_from_this<App>::shared_from_this;

    boost::asio::io_service& ioService_;
    boost::asio::io_service::strand strand_;
    service::IAndroidAutoEntityFactory& androidAutoEntityFactory_;
    service::IAndroidAutoEntity::Pointer androidAutoEntity_;
    bool isStopped_;
};

}
}
}
