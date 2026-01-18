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

#include <btservice/BluezBluetoothService.hpp>
#include <Common/Log.hpp>

namespace f1x::openauto::btservice {

  BluezBluetoothService::BluezBluetoothService() {
    OPENAUTO_LOG(info) << "[BluezBluetoothService] Initialising";
  }

  bool BluezBluetoothService::registerService(int16_t portNumber, const QBluetoothAddress& bluetoothAddress) {
    OPENAUTO_LOG(info) << "[BluezBluetoothService] registerService handled by BlueZ profile (port="
                       << portNumber << ", address=" << bluetoothAddress.toString().toStdString() << ")";
    return true;
  }

  bool BluezBluetoothService::unregisterService() {
    OPENAUTO_LOG(info) << "[BluezBluetoothService] unregisterService is a no-op (handled by BlueZ)";
    return true;
  }

}
