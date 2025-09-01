/*
 *  This file is part of OpenAutoCore project.
 *  Copyright (C) 2025 buzzcola3 (Samuel Betak)
 *
 *  OpenAutoCore is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenAutoCore is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.
 */

#include <f1x/openauto/autoapp/Projection/SharedAudioOutput.hpp>
#include <buzz/autoapp/Projection/SharedMemoryProducer.hpp>
#include <f1x/openauto/Common/Log.hpp>
#include <cstring>
#include <boost/asio/io_context.hpp>
#include <memory>
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

SharedAudioOutput::SharedAudioOutput(boost::asio::io_context& io_context, uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate)
    : channelCount_(channelCount)
    , sampleSize_(sampleSize)
    , sampleRate_(sampleRate)
{
    size_t buffer_capacity = 8192 + 12; 
    std::string shmName = "/openauto_audio_shm";
    std::string semName = "/sem.openauto_audio_shm_sem";
    audioProducer_ = std::make_unique<SharedMemoryProducer>(shmName, semName, buffer_capacity);

    if (!audioProducer_->init()) {
        OPENAUTO_LOG(error) << "[SharedAudioOutput] Failed to initialize SharedMemoryProducer!";
    } else {
        OPENAUTO_LOG(info) << "[SharedAudioOutput] Created. Sample Rate: " << sampleRate_;
    }
}

bool SharedAudioOutput::open()
{
    return true;
}

void SharedAudioOutput::write(aasdk::messenger::Timestamp::ValueType timestamp, const aasdk::common::DataConstBuffer& buffer)
{
    // Compose header (timestamp + size) + payload into a single buffer
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
    oss << "[SharedAudioOutput] Header bytes: ";
    for (size_t i = 0; i < header_size; ++i) {
        oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
            << static_cast<int>(outbuf[i]) << " ";
    }
    OPENAUTO_LOG(info) << oss.str();

    audioProducer_->writeBuffer(outbuf.data(), outbuf.size());

    OPENAUTO_LOG(info) << "[SharedAudioOutput] Wrote chunk with timestamp " << timestamp
                       << " and size " << buffer.size;
}

void SharedAudioOutput::start()
{
    // Nothing to do.
}

void SharedAudioOutput::stop()
{
    // No-op: Producer manages its own queue and memory.
}

void SharedAudioOutput::suspend()
{
    // No-op: Producer manages its own queue and memory.
}

uint32_t SharedAudioOutput::getSampleSize() const
{
    return sampleSize_;
}

uint32_t SharedAudioOutput::getChannelCount() const
{
    return channelCount_;
}

uint32_t SharedAudioOutput::getSampleRate() const
{
    return sampleRate_;
}

}
}
}
}