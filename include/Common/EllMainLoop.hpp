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

#include <atomic>
#include <mutex>

namespace f1x::openauto::common {

class EllMainLoop {
public:
    static EllMainLoop& instance();

    /// Initialise ELL (l_main_init). Safe to call multiple times.
    void ensureRunning();

    /// Non-blocking: process any pending ELL events.
    void step();

    void shutdown();

private:
    EllMainLoop();
    ~EllMainLoop();

    EllMainLoop(const EllMainLoop&) = delete;
    EllMainLoop& operator=(const EllMainLoop&) = delete;

    std::atomic_bool initialized_{false};
    std::mutex mutex_;
};

}
