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

#include <string>

// AOAP accessory identification strings sent to the Android device during
// the USB accessory handshake.  The model string ("Android Auto") is what
// triggers the phone OS to launch the Android Auto companion app.

struct AoapConfig {
    std::string manufacturer  = "Android";
    std::string model         = "Android Auto";
    std::string description   = "Android Auto";
    std::string version       = "2.0.1";
    std::string uri           = "https://f1xstudio.com";
    std::string serial        = "HU-AAAAAA001";
};
