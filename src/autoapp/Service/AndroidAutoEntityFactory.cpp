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

#include <USB/AOAPDevice.hpp>
#include <Transport/SSLWrapper.hpp>
#include <Transport/USBTransport.hpp>
#include <Transport/TCPTransport.hpp>
#include <Messenger/Cryptor.hpp>
#include <Messenger/MessageInStream.hpp>
#include <Messenger/MessageOutStream.hpp>
#include <Messenger/Messenger.hpp>
#include <f1x/openauto/autoapp/Service/AndroidAutoEntityFactory.hpp>
#include <f1x/openauto/autoapp/Service/AndroidAutoEntity.hpp>
#include <f1x/openauto/autoapp/Service/Pinger.hpp>

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {

        using IoContext = boost::asio::io_context;
        using Strand = boost::asio::strand<IoContext::executor_type>;

        AndroidAutoEntityFactory::AndroidAutoEntityFactory(IoContext &ioContext,
                                                           configuration::IConfiguration::Pointer configuration,
                                                           IServiceFactory &serviceFactory)
            : ioContext_(ioContext), configuration_(std::move(configuration)), serviceFactory_(serviceFactory) {

        }

        IAndroidAutoEntity::Pointer AndroidAutoEntityFactory::create(aasdk::usb::IAOAPDevice::Pointer aoapDevice) {
          auto transport(std::make_shared<aasdk::transport::USBTransport>(ioContext_, std::move(aoapDevice)));
          return create(std::move(transport));
        }

        IAndroidAutoEntity::Pointer AndroidAutoEntityFactory::create(aasdk::tcp::ITCPEndpoint::Pointer tcpEndpoint) {
          auto transport(std::make_shared<aasdk::transport::TCPTransport>(ioContext_, std::move(tcpEndpoint)));
          return create(std::move(transport));
        }

        IAndroidAutoEntity::Pointer AndroidAutoEntityFactory::create(aasdk::transport::ITransport::Pointer transport) {
          auto sslWrapper(std::make_shared<aasdk::transport::SSLWrapper>());
          auto cryptor(std::make_shared<aasdk::messenger::Cryptor>(std::move(sslWrapper)));
          cryptor->init();

          auto messenger(std::make_shared<aasdk::messenger::Messenger>(ioContext_,
                                                                       std::make_shared<aasdk::messenger::MessageInStream>(
                                                                           ioContext_, transport, cryptor),
                                                                       std::make_shared<aasdk::messenger::MessageOutStream>(
                                                                           ioContext_, transport, cryptor)));

          auto serviceList = serviceFactory_.create(messenger);
          auto pinger(std::make_shared<Pinger>(ioContext_, 5000));
          return std::make_shared<AndroidAutoEntity>(ioContext_, std::move(cryptor), std::move(transport),
                                                     std::move(messenger), configuration_, std::move(serviceList),
                                                     std::move(pinger));
        }

      }
    }
  }
}
