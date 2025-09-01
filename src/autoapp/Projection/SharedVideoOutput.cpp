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
#include <buzz/autoapp/Projection/SharedMemoryProducer.hpp>
#include <buzz/autoapp/Transport/transport.hpp>
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
    size_t buffer_capacity = MAX_VIDEO_CHUNK_SIZE;
    std::string shmName = "/openauto_video_shm";
    std::string semName = "/sem.openauto_video_shm_sem";
    videoProducer_ = std::make_unique<SharedMemoryProducer>(shmName, semName, buffer_capacity);

    if (!videoProducer_->init()) {
        OPENAUTO_LOG(error) << "[SharedVideoOutput] Failed to initialize SharedMemoryProducer!";
    } else {
        OPENAUTO_LOG(info) << "[SharedVideoOutput] Created.";
    }
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
    const size_t header_size = sizeof(uint64_t) + sizeof(uint32_t);
    const size_t total_size = header_size + buffer.size;
    std::vector<unsigned char> outbuf(total_size);

    // Write timestamp
    std::memcpy(outbuf.data(), &timestamp, sizeof(uint64_t));
    // Write size
    uint32_t chunk_size = static_cast<uint32_t>(buffer.size);
    std::memcpy(outbuf.data() + sizeof(uint64_t), &chunk_size, sizeof(uint32_t));
    // Write payload
    std::memcpy(outbuf.data() + header_size, buffer.cdata, buffer.size);

    // Print header bytes (timestamp + size)
    std::ostringstream oss;
    oss << "[SharedVideoOutput] Header bytes: ";
    for (size_t i = 0; i < header_size; ++i) {
        oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(outbuf[i]) << " ";
    }
    OPENAUTO_LOG(info) << oss.str();

    // Write to SHM producer (existing path)
    videoProducer_->writeBuffer(outbuf.data(), outbuf.size());

    // Also send via Transport as a minimal Envelope (MsgType::video = 0)
    if (transport_) {
        constexpr uint16_t kMsgTypeVideo = 0; // matches wire.capnp enum MsgType.video
        transport_->send(kMsgTypeVideo, timestamp, buffer.cdata, buffer.size);
    }

    OPENAUTO_LOG(info) << "[SharedVideoOutput] Wrote chunk with timestamp " << timestamp
                       << " and size " << buffer.size;
}

void SharedVideoOutput::stop()
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Stopped.";
    // No-op: Producer manages its own queue and memory.
}

}
}
}
}