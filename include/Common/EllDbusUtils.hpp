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
