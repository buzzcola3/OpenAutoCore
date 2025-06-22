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

#pragma once

#include <f1x/openauto/autoapp/Projection/IAudioOutput.hpp>
#include <f1x/openauto/autoapp/Projection/SequentialBuffer.hpp>
#include <mutex>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

/**
 * @brief An IAudioOutput implementation that stores audio data in a shared,
 *        asynchronous buffer instead of playing it directly.
 */
class SharedAudioOutput: public IAudioOutput
{
public:
    SharedAudioOutput(boost::asio::io_context& io_context, uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate);
    ~SharedAudioOutput() override = default;

    // IAudioOutput interface implementation
    bool open() override;
    void write(aasdk::messenger::Timestamp::ValueType timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void start() override;
    void stop() override;
    void suspend() override;
    uint32_t getSampleSize() const override;
    uint32_t getChannelCount() const override;
    uint32_t getSampleRate() const override;

    /**
     * @brief Provides access to the underlying buffer for a consumer.
     * @return A reference to the SequentialBuffer.
     */
    SequentialBuffer& getBuffer();

private:
    uint32_t channelCount_;
    uint32_t sampleSize_;
    uint32_t sampleRate_;
    SequentialBuffer audioBuffer_;
};

}
}
}
}