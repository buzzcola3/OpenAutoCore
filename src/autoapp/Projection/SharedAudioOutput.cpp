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
#include <f1x/openauto/Common/Log.hpp>
#include <boost/asio/io_context.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SharedAudioOutput::SharedAudioOutput(boost::asio::io_context& /*io_context*/,
                                     uint32_t channelCount,
                                     uint32_t sampleSize,
                                     uint32_t sampleRate)
    : channelCount_(channelCount)
    , sampleSize_(sampleSize)
    , sampleRate_(sampleRate)
{
    OPENAUTO_LOG(info) << "[SharedAudioOutput] Dummy created. Sample Rate: " << sampleRate_;
}

bool SharedAudioOutput::open()
{
    return true;
}

void SharedAudioOutput::write(aasdk::messenger::Timestamp::ValueType timestamp,
                              const aasdk::common::DataConstBuffer& buffer)
{
    OPENAUTO_LOG(info) << "[SharedAudioOutput] Dummy write ts=" << timestamp
                       << " size=" << buffer.size;
}

void SharedAudioOutput::start()
{
    // no-op
}

void SharedAudioOutput::stop()
{
    // no-op
}

void SharedAudioOutput::suspend()
{
    // no-op
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