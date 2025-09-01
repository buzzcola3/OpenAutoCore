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

#pragma once

#include <f1x/openauto/autoapp/Projection/IAudioOutput.hpp>
#include <buzz/autoapp/Projection/SharedMemoryProducer.hpp> // <-- Use the producer
#include <cstdint>
#include <boost/asio/io_context.hpp>
#include <memory>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

constexpr size_t MAX_AUDIO_CHUNK_SIZE = 8192+12; // 8KB for audio data + 12 bytes for header (timestamp + size)

class SharedAudioOutput: public IAudioOutput
{
public:
    SharedAudioOutput(boost::asio::io_context& io_context, uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate);
    ~SharedAudioOutput() override = default;

    bool open() override;
    void write(aasdk::messenger::Timestamp::ValueType timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void start() override;
    void stop() override;
    void suspend() override;
    uint32_t getSampleSize() const override;
    uint32_t getChannelCount() const override;
    uint32_t getSampleRate() const override;

private:
    uint32_t channelCount_;
    uint32_t sampleSize_;
    uint32_t sampleRate_;
    std::unique_ptr<SharedMemoryProducer> audioProducer_; // <-- Use the producer
};

}
}
}
}