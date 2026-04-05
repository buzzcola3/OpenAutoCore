/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*  Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
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

#include <Service/Pinger.hpp>
#include <Common/Log.hpp>

namespace f1x::openauto::autoapp::service {

  Pinger::Pinger(boost::asio::io_service &ioService, time_t duration)
      : strand_(ioService), timer_(ioService), duration_(duration), cancelled_(false), pingsCount_(0), pongsCount_(0) {
  }

  void Pinger::start() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      cancelled_ = false;
      ++pingsCount_;
      OPENAUTO_LOG(debug) << "[Pinger] Ping counter: " << pingsCount_;
      timer_.expires_from_now(boost::posix_time::milliseconds(duration_));
      timer_.async_wait(
          strand_.wrap(std::bind(&Pinger::onTimerExceeded, this->shared_from_this(), std::placeholders::_1)));
    });
  }

  void Pinger::pong() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      ++pongsCount_;
      OPENAUTO_LOG(debug) << "[Pinger] Pong counter: " << pongsCount_;
    });
  }

  void Pinger::onTimerExceeded(const boost::system::error_code &error) {
    if (error == boost::asio::error::operation_aborted || cancelled_) {
      return;
    }

    if (pingsCount_ - pongsCount_ > 4) {
      if (onPingTimeout) onPingTimeout();
      return;
    }

    if (onPingReady) onPingReady();

    // Self-sustaining: schedule next cycle
    ++pingsCount_;
    timer_.expires_from_now(boost::posix_time::milliseconds(duration_));
    timer_.async_wait(
        strand_.wrap(std::bind(&Pinger::onTimerExceeded, this->shared_from_this(), std::placeholders::_1)));
  }

  void Pinger::cancel() {
    strand_.dispatch([this, self = this->shared_from_this()]() {
      cancelled_ = true;
      timer_.cancel();
    });
  }

}



