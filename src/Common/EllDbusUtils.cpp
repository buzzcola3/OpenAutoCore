#include <Common/EllDbusUtils.hpp>
#include <Common/Log.hpp>
#include <condition_variable>
#include <ell/dbus.h>
#include <ell/main.h>
#include <mutex>

namespace f1x::openauto::common {

namespace {

struct ReadyWaiter {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready{false};
};

void readyHandler(void* userData) {
    auto* waiter = static_cast<ReadyWaiter*>(userData);
    if (waiter == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(waiter->mutex);
    waiter->ready = true;
    waiter->cv.notify_one();
    OPENAUTO_LOG(info) << "[EllDbus] System bus ready";
}

struct ReplyWaiter {
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
    l_dbus_message* reply{nullptr};
};

void logMessageDetails(const char* prefix, l_dbus_message* message) {
    if (message == nullptr) {
        OPENAUTO_LOG(warning) << prefix << " message=null";
        return;
    }

    const char* destination = l_dbus_message_get_destination(message);
    const char* path = l_dbus_message_get_path(message);
    const char* interfaceName = l_dbus_message_get_interface(message);
    const char* member = l_dbus_message_get_member(message);
    const char* signature = l_dbus_message_get_signature(message);

    OPENAUTO_LOG(debug) << prefix
                        << " dest=" << (destination != nullptr ? destination : "")
                        << " path=" << (path != nullptr ? path : "")
                        << " iface=" << (interfaceName != nullptr ? interfaceName : "")
                        << " member=" << (member != nullptr ? member : "")
                        << " sig=" << (signature != nullptr ? signature : "");
}

void logDbusError(const char* prefix, l_dbus_message* message) {
    if (message == nullptr) {
        return;
    }

    if (!l_dbus_message_is_error(message)) {
        return;
    }

    const char* errName = nullptr;
    const char* errText = nullptr;
    l_dbus_message_get_error(message, &errName, &errText);
    OPENAUTO_LOG(warning) << prefix
                          << " error=" << (errName != nullptr ? errName : "unknown")
                          << " text=" << (errText != nullptr ? errText : "");
}

void replyHandler(l_dbus_message* message, void* userData) {
    auto* waiter = static_cast<ReplyWaiter*>(userData);
    if (waiter == nullptr) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(waiter->mutex);
        waiter->reply = message != nullptr ? l_dbus_message_ref(message) : nullptr;
        waiter->done = true;
    }

    logMessageDetails("[EllDbus] Reply", message);
    logDbusError("[EllDbus] Reply error", message);
    waiter->cv.notify_one();
}

}

bool ellDbusWaitReady(l_dbus* bus, std::chrono::milliseconds timeout) {
    if (bus == nullptr) {
        return false;
    }

    ReadyWaiter waiter;
    if (!l_dbus_set_ready_handler(bus, readyHandler, &waiter, nullptr)) {
        return false;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(waiter.mutex);
    while (!waiter.ready) {
        if (waiter.cv.wait_until(lock, deadline, [&waiter]() { return waiter.ready; })) {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            OPENAUTO_LOG(warning) << "[EllDbus] Timed out waiting for DBus ready";
            return false;
        }
    }

    return true;
}

l_dbus_message* ellDbusSendWithReplySync(l_dbus* bus,
                                        l_dbus_message* message,
                                        std::chrono::milliseconds timeout) {
    if (bus == nullptr || message == nullptr) {
        return nullptr;
    }

    ReplyWaiter waiter;
    logMessageDetails("[EllDbus] Sending", message);

    const auto serial = l_dbus_send_with_reply(bus, message, replyHandler, &waiter, nullptr);
    if (serial == 0) {
        l_dbus_message_unref(message);
        return nullptr;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unique_lock<std::mutex> lock(waiter.mutex);
    while (!waiter.done) {
        if (waiter.cv.wait_until(lock, deadline, [&waiter]() { return waiter.done; })) {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            logMessageDetails("[EllDbus] Timeout waiting reply for", message);
            return nullptr;
        }
    }

    return waiter.reply;
}

bool ellDbusNameHasOwner(l_dbus* bus,
                         const char* name,
                         std::chrono::milliseconds timeout) {
    if (bus == nullptr || name == nullptr) {
        return false;
    }

    auto* msg = l_dbus_message_new_method_call(bus,
                                               "org.freedesktop.DBus",
                                               "/org/freedesktop/DBus",
                                               "org.freedesktop.DBus",
                                               "NameHasOwner");
    auto* builder = l_dbus_message_builder_new(msg);
    l_dbus_message_builder_append_basic(builder, 's', name);
    l_dbus_message_builder_finalize(builder);
    l_dbus_message_builder_destroy(builder);

    auto* reply = ellDbusSendWithReplySync(bus, msg, timeout);
    if (reply == nullptr || l_dbus_message_is_error(reply)) {
        logDbusError("[EllDbus] NameHasOwner failed", reply);
        if (reply != nullptr) {
            l_dbus_message_unref(reply);
        }
        return false;
    }

    bool hasOwner = false;
    if (!l_dbus_message_get_arguments(reply, "b", &hasOwner)) {
        l_dbus_message_unref(reply);
        return false;
    }

    l_dbus_message_unref(reply);
    return hasOwner;
}

}
