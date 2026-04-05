// Copyright (C) 2024 CubeOne (Simon Dean - simon.dean@cubeone.co.uk)
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

#include <sstream>
#include <string>

enum class DmLogLevel { info, warning, error };

// Pluggable log callback. Set via dm_set_log_callback().
// Default: writes to stderr.
using DmLogCallback = void(*)(DmLogLevel level, const std::string& message);
void dm_set_log_callback(DmLogCallback cb);

// RAII log stream — collects the message via operator<<, then fires the
// callback (or stderr fallback) in the destructor.
class DmLogStream {
public:
    explicit DmLogStream(DmLogLevel level) : level_(level) {}
    ~DmLogStream();

    DmLogStream(const DmLogStream&) = delete;
    DmLogStream& operator=(const DmLogStream&) = delete;
    DmLogStream(DmLogStream&&) = default;

    template<typename T>
    DmLogStream& operator<<(const T& v) { ss_ << v; return *this; }

    // Support stream manipulators (std::hex, std::dec, etc.)
    DmLogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        manip(ss_);
        return *this;
    }

private:
    DmLogLevel level_;
    std::ostringstream ss_;
};

#define DM_LOG(level) DmLogStream(DmLogLevel::level)
