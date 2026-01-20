#include <Common/EllMainLoop.hpp>
#include <Common/Log.hpp>
#include <ell/main.h>

namespace f1x::openauto::common {

EllMainLoop::EllMainLoop() = default;

EllMainLoop::~EllMainLoop() {
    shutdown();
}

EllMainLoop& EllMainLoop::instance() {
    static EllMainLoop instance;
    return instance;
}

void EllMainLoop::ensureRunning() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) {
        return;
    }

    if (!l_main_init()) {
        OPENAUTO_LOG(error) << "[EllMainLoop] Failed to initialize l_main";
        return;
    }

    running_.store(true);
    loopThread_ = std::thread([this]() {
        while (running_.load()) {
            l_main_iterate(50);
        }
    });
}

void EllMainLoop::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    l_main_exit();
    if (loopThread_.joinable()) {
        loopThread_.join();
    }
}

}
