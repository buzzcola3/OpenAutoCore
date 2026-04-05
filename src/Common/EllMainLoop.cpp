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
