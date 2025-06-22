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

#include <f1x/openauto/autoapp/Projection/SharedAudioOutput.hpp>
#include <f1x/openauto/Common/Log.hpp>

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
    , audioBuffer_(io_context)
{
    OPENAUTO_LOG(info) << "[SharedAudioOutput] Created. Sample Rate: " << sampleRate_;
}

bool SharedAudioOutput::open()
{
    // Nothing to do to open a buffer.
    return true;
}

void SharedAudioOutput::write(aasdk::messenger::Timestamp::ValueType timestamp, const aasdk::common::DataConstBuffer& buffer)
{
    OPENAUTO_LOG(info) << "[SharedAudioOutput] Writing " << buffer.size << " bytes of audio data.";
    audioBuffer_.write(reinterpret_cast<const char*>(buffer.cdata), buffer.size);
}

void SharedAudioOutput::start()
{
    // Nothing to do to start a buffer.
}

void SharedAudioOutput::stop()
{
    // Closing the buffer signals the end of the stream to any readers.
    audioBuffer_.close();
}

void SharedAudioOutput::suspend()
{
    // For a buffer, suspend could mean clearing it.
    // For now, we do nothing.
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

SequentialBuffer& SharedAudioOutput::getBuffer()
{
    return audioBuffer_;
}

}
}
}
}