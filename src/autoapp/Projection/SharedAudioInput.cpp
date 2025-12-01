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
#include <cstdint>
#include <utility>

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

namespace {
constexpr uint32_t kFrameDurationMs = 20;

std::size_t ComputeFrameSizeBytes(uint32_t channelCount, uint32_t sampleSizeBits, uint32_t sampleRateHz)
{
    const auto bytesPerSample = static_cast<std::size_t>(std::max<uint32_t>(1u, (sampleSizeBits + 7u) / 8u));
    const auto samplesPerFrame = static_cast<std::size_t>(std::max<uint32_t>(1u, (sampleRateHz * kFrameDurationMs) / 1000u));
    return std::max<std::size_t>(bytesPerSample * channelCount * samplesPerFrame, bytesPerSample);
}
}

SharedAudioInput::SharedAudioInput(uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate)
    : channelCount_(channelCount)
    , sampleSize_(sampleSize)
    , sampleRate_(sampleRate)
    , frameSizeBytes_(ComputeFrameSizeBytes(channelCount, sampleSize, sampleRate))
{
    OPENAUTO_LOG(info) << "[SharedAudioInput] Silent audio input created with frame size " << frameSizeBytes_ << " bytes.";
}

bool SharedAudioInput::open()
{
    opened_.store(true, std::memory_order_relaxed);
    return true;
}

bool SharedAudioInput::isActive() const
{
    return active_.load(std::memory_order_relaxed);
}

void SharedAudioInput::read(ReadPromise::Pointer promise)
{
    if (!active_.load(std::memory_order_relaxed))
    {
        promise->reject();
        return;
    }

    aasdk::common::Data buffer(frameSizeBytes_, 0U);
    promise->resolve(std::move(buffer));
}

void SharedAudioInput::start(StartPromise::Pointer promise)
{
    if (!opened_.load(std::memory_order_relaxed))
    {
        opened_.store(true, std::memory_order_relaxed);
    }

    active_.store(true, std::memory_order_relaxed);
    promise->resolve();
}

void SharedAudioInput::stop()
{
    active_.store(false, std::memory_order_relaxed);
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