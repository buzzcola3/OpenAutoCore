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

#include <iostream>
#include <DeviceManager/Common/DmLog.hpp>

static DmLogCallback g_logCallback = nullptr;

void dm_set_log_callback(DmLogCallback cb) {
    g_logCallback = cb;
}

static const char* levelStr(DmLogLevel level) {
    switch (level) {
        case DmLogLevel::info:    return "INFO";
        case DmLogLevel::warning: return "WARNING";
        case DmLogLevel::error:   return "ERROR";
    }
    return "?";
}

DmLogStream::~DmLogStream() {
    if (g_logCallback) {
        g_logCallback(level_, ss_.str());
    } else {
        std::cerr << "[DeviceManager][" << levelStr(level_) << "] "
                  << ss_.str() << std::endl;
    }
}
