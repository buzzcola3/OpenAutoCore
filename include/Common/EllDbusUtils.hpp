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

#pragma once

#include <chrono>

struct l_dbus;
struct l_dbus_message;

namespace f1x::openauto::common {

bool ellDbusWaitReady(l_dbus* bus, std::chrono::milliseconds timeout);

l_dbus_message* ellDbusSendWithReplySync(l_dbus* bus,
                                        l_dbus_message* message,
                                        std::chrono::milliseconds timeout);

bool ellDbusNameHasOwner(l_dbus* bus,
                         const char* name,
                         std::chrono::milliseconds timeout);

}
