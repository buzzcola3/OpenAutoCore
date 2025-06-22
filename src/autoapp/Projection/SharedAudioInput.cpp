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
    // A dummy device is never "active".
    return false;
}

void SharedAudioInput::read(ReadPromise::Pointer promise)
{
    // A dummy device never provides data, so we reject the promise
    // to prevent the caller from waiting forever.
    promise->reject();
}

void SharedAudioInput::start(StartPromise::Pointer promise)
{
    // A dummy device can "start" successfully without doing anything.
    promise->resolve();
}

void SharedAudioInput::stop()
{
    // Nothing to do.
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