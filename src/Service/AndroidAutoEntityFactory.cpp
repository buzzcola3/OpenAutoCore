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

#include <Transport/SSLWrapper.hpp>
#include <Messenger/Cryptor.hpp>
#include <Messenger/MessageInStreamInterceptor.hpp>
#include <FrameRouter.hpp>
#include <Service/AndroidAutoEntityFactory.hpp>
#include <Service/AndroidAutoEntity.hpp>
#include <Service/Pinger.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {

        AndroidAutoEntityFactory::AndroidAutoEntityFactory(boost::asio::io_service &ioService,
                                                           configuration::IConfiguration::Pointer configuration,
                                                           configuration::ServiceConfig &serviceConfig,
                                                           IServiceFactory &serviceFactory)
            : ioService_(ioService), configuration_(std::move(configuration)), serviceConfig_(serviceConfig), serviceFactory_(serviceFactory) {

        }

        IAndroidAutoEntity::Pointer AndroidAutoEntityFactory::create(DeviceConnection::Pointer connection) {
          auto sslWrapper(std::make_shared<aasdk::transport::SSLWrapper>());
          auto cryptor(std::make_shared<aasdk::messenger::Cryptor>(std::move(sslWrapper)));
          cryptor->init();

          auto router = std::make_shared<aasdk::FrameRouter>(std::move(connection), cryptor);

          // Set the FrameRouter's send function as the global send path for all handlers
          aasdk::messenger::interceptor::setSendFn(router->makeSendFn());

          auto serviceList = serviceFactory_.create();
          auto pinger(std::make_shared<Pinger>(ioService_, 5000));
          return std::make_shared<AndroidAutoEntity>(ioService_, std::move(cryptor),
                                                     std::move(router), configuration_,
                                                     serviceConfig_, std::move(serviceList),
                                                     std::move(pinger));
        }

      }
    }
  }
}
