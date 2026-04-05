// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

// Interceptor — handler dispatch and global Lite handler registry.

#pragma once

#include <functional>
#include <memory>
#include <Lite/FrameIO.hpp>

namespace buzz { namespace autoapp { namespace Transport { class Transport; } } }

namespace aasdk::lite {
class BluetoothHandler;
class MediaSourceHandler;
class InputSourceHandler;
class SensorHandler;
class ControlHandler;
}

namespace aasdk::messenger::interceptor {

void handleMessage(const ::aasdk::lite::InMessage& message);
void setSendFn(::aasdk::lite::SendFn fn);
void setVideoTransport(const std::shared_ptr<buzz::autoapp::Transport::Transport>& transport);
::aasdk::lite::InputSourceHandler& getInputSourceHandler();
::aasdk::lite::SensorHandler& getSensorHandler();
::aasdk::lite::BluetoothHandler& getBluetoothHandler();
::aasdk::lite::MediaSourceHandler& getMediaSourceHandler();
::aasdk::lite::ControlHandler& getControlHandler();

}
