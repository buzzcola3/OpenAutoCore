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

#include <f1x/openauto/autoapp/Projection/IAudioInput.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

// A dummy implementation of IAudioInput that does not depend on any framework.
class SharedAudioInput : public IAudioInput
{
public:
    SharedAudioInput(uint32_t channelCount, uint32_t sampleSize, uint32_t sampleRate);
    ~SharedAudioInput() override = default;

    bool open() override;
    bool isActive() const override;
    void read(ReadPromise::Pointer promise) override;
    void start(StartPromise::Pointer promise) override;
    void stop() override;
    uint32_t getSampleSize() const override;
    uint32_t getChannelCount() const override;
    uint32_t getSampleRate() const override;

private:
    uint32_t channelCount_;
    uint32_t sampleSize_;
    uint32_t sampleRate_;
};

}
}
}
}