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

#include <algorithm>

#include <f1x/openauto/autoapp/Projection/SharedAudioInput.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SharedAudioInput::SharedAudioInput(uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate)
    : channelCount_(channelCount)
    , sampleSize_(sampleSize)
    , sampleRate_(sampleRate)
    , active_(false)
{
    OPENAUTO_LOG(info) << "[SharedAudioInput] Dummy audio input created.";
}

bool SharedAudioInput::open()
{
    // A dummy device is always "open".
    return true;
}

bool SharedAudioInput::isActive() const
{
    return active_;
}

void SharedAudioInput::read(ReadPromise::Pointer promise)
{
    if(!active_)
    {
        promise->reject();
        return;
    }

    // Produce ~10 ms of silence to keep the channel flowing.
    const uint32_t framesPerChunk = std::max(1u, sampleRate_ / 100u);
    const uint32_t bytesPerSample = sampleSize_ / 8;
    const size_t bufferSize = static_cast<size_t>(framesPerChunk) * channelCount_ * bytesPerSample;

    aasdk::common::Data buffer(bufferSize, 0);
    promise->resolve(std::move(buffer));
}

void SharedAudioInput::start(StartPromise::Pointer promise)
{
    active_ = true;
    promise->resolve();
}

void SharedAudioInput::stop()
{
    active_ = false;
}

uint32_t SharedAudioInput::getSampleSize() const
{
    return sampleSize_;
}

uint32_t SharedAudioInput::getChannelCount() const
{
    return channelCount_;
}

uint32_t SharedAudioInput::getSampleRate() const
{
    return sampleRate_;
}

}
}
}
}