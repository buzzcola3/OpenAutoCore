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
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SharedVideoOutput::SharedVideoOutput(boost::asio::io_context& io_context, configuration::IConfiguration::Pointer configuration)
    : VideoOutput(std::move(configuration))
    , videoBuffer_(io_context) // Initialize the buffer with the io_context
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Created.";
}

bool SharedVideoOutput::open()
{
    // The buffer is memory-based and is always "open".
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Opened.";
    return true;
}

bool SharedVideoOutput::init()
{
    // No initialization is needed for a simple buffer sink.
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Initialized.";
    return true;
}

void SharedVideoOutput::stop()
{
    // Close the buffer to signal EOF to any readers.
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Stopped.";
    videoBuffer_.close();
}

void SharedVideoOutput::write(uint64_t /*timestamp*/, const aasdk::common::DataConstBuffer& buffer)
{
    // Write the received video data directly into the sequential buffer.
    videoBuffer_.write(reinterpret_cast<const char*>(buffer.cdata), buffer.size);
}

SequentialBuffer& SharedVideoOutput::getBuffer()
{
    return videoBuffer_;
}

}
}
}
}