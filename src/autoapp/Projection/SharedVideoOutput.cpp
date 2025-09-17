/*
*  This file is part of openauto project.
*  Copyright (C) 2025 buzzcola3 (Samuel Betak)
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

#include <f1x/openauto/autoapp/Projection/SharedVideoOutput.hpp>
#include <buzz/autoapp/Transport/transport.hpp>
#include <buzz/autoapp/Transport/wire.hpp>
#include <f1x/openauto/Common/Log.hpp>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SharedVideoOutput::SharedVideoOutput(boost::asio::io_context& io_context,
                                     configuration::IConfiguration::Pointer configuration,
                                     std::shared_ptr<buzz::autoapp::Transport::Transport> transport)
    : VideoOutput(std::move(configuration))
    , transport_(std::move(transport))
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Created (transport-only).";
}

bool SharedVideoOutput::open()
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Opened.";
    return true;
}

bool SharedVideoOutput::init()
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Initialized.";
    return true;
}

void SharedVideoOutput::write(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer)
{
    // Send via Transport as an Envelope (MsgType::video). Version is taken from schema default.
    if (buffer.cdata != nullptr && buffer.size > 0) {
        transport_->send(buzz::wire::MsgType::VIDEO, timestamp, buffer.cdata, buffer.size);
    }

    OPENAUTO_LOG(info) << "[SharedVideoOutput] Wrote chunk with timestamp " << timestamp
                       << " and size " << buffer.size;
}

void SharedVideoOutput::stop()
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Stopped.";
    // No-op.
}

}
}
}
}