/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <Projection/IBluetoothDevice.hpp>
#include <ell/dbus.h>
#include <memory>
#include <string>

namespace f1x::openauto::autoapp::projection {

class BluezBluetoothDevice : public IBluetoothDevice
{
public:
    explicit BluezBluetoothDevice(std::string adapterAddress);

    void stop() override;
    bool isPaired(const std::string& address) const override;
    std::string getAdapterAddress() const override;
    bool isAvailable() const override;

private:
    std::string resolveAdapterPath() const;
    bool getDevicePaired(const std::string& deviceAddress) const;

    struct EllDbusDeleter {
        void operator()(l_dbus* bus) const {
            if (bus != nullptr) {
                l_dbus_destroy(bus);
            }
        }
    };

    std::string adapterAddress_;
    std::unique_ptr<l_dbus, EllDbusDeleter> bus_;
};

}
