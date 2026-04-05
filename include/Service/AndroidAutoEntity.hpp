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

#pragma once

#include <boost/asio.hpp>
#include <Transport/ITransport.hpp>
#include <Messenger/ICryptor.hpp>
#include <Messenger/IMessenger.hpp>
#include <Configuration/IConfiguration.hpp>
#include <Configuration/ServiceConfig.hpp>
#include <Service/IAndroidAutoEntity.hpp>
#include <Service/IService.hpp>
#include <Service/IPinger.hpp>
#include <aap_protobuf/service/control/message/AudioFocusRequestType.pb.h>
#include <aap_protobuf/service/control/message/AudioFocusStateType.pb.h>
#include <aap_protobuf/service/control/message/NavFocusType.pb.h>

namespace aasdk::lite { class ControlHandler; }

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace service
{

class AndroidAutoEntity: public IAndroidAutoEntity, public std::enable_shared_from_this<AndroidAutoEntity>
{
public:
    AndroidAutoEntity(boost::asio::io_service& ioService,
                      aasdk::messenger::ICryptor::Pointer cryptor,
                      aasdk::transport::ITransport::Pointer transport,
                      aasdk::messenger::IMessenger::Pointer messenger,
                      configuration::IConfiguration::Pointer configuration,
                      configuration::ServiceConfig& serviceConfig,
                      ServiceList serviceList,
                      IPinger::Pointer pinger);
    ~AndroidAutoEntity() override;

    void start(IAndroidAutoEntityEventHandler& eventHandler) override;
    void stop() override;
    void pause() override;
    void resume() override;

private:
    using std::enable_shared_from_this<AndroidAutoEntity>::shared_from_this;
    void triggerQuit();
    void schedulePing();
    void sendPing();
    void wireControlCallbacks();

    boost::asio::io_service::strand strand_;
    aasdk::messenger::ICryptor::Pointer cryptor_;
    aasdk::transport::ITransport::Pointer transport_;
    aasdk::messenger::IMessenger::Pointer messenger_;
    aasdk::lite::ControlHandler& controlHandler_;
    configuration::IConfiguration::Pointer configuration_;
    configuration::ServiceConfig& serviceConfig_;
    ServiceList serviceList_;
    IPinger::Pointer pinger_;
    IAndroidAutoEntityEventHandler* eventHandler_;
};

}
}
}
}
