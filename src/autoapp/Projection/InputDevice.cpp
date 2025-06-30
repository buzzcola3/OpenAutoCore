/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
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

#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/Projection/IInputDeviceEventHandler.hpp>
#include <f1x/openauto/autoapp/Projection/InputDevice.hpp>
#include <algorithm>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

InputDevice::InputDevice(configuration::IConfiguration::Pointer configuration, const buzz::common::Rect& touchscreenGeometry, const buzz::common::Rect& displayGeometry)
    : configuration_(std::move(configuration))
    , touchscreenGeometry_(touchscreenGeometry)
    , displayGeometry_(displayGeometry)
    , eventHandler_(nullptr)
{
}

void InputDevice::start(IInputDeviceEventHandler& eventHandler)
{
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    //OPENAUTO_LOG(info) << "[InputDevice] start()";
    eventHandler_ = &eventHandler;
}

void InputDevice::stop()
{
    std::lock_guard<decltype(mutex_)> lock(mutex_);

    OPENAUTO_LOG(info) << "[InputDevice] stop()";
    eventHandler_ = nullptr;
}


bool InputDevice::hasTouchscreen() const
{
    return configuration_->getTouchscreenEnabled();
}

buzz::common::Rect InputDevice::getTouchscreenGeometry() const
{
    return touchscreenGeometry_;
}

IInputDevice::ButtonCodes InputDevice::getSupportedButtonCodes() const
{
    return configuration_->getButtonCodes();
}

}
}
}
}
