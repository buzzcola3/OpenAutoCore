/*
*  This file is part of openauto project.
*  Copyright (C) 2025 buzzcola3 (Samuel Betak)
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

#include <f1x/openauto/autoapp/Projection/SharedVideoOutput.hpp>
#include "open_auto_transport/transport.hpp"
#include "wire.hpp"
#include <f1x/openauto/Common/Log.hpp>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace {
constexpr uint64_t kQueueDropLogMask = 0x3F;
}

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SharedVideoOutput::SharedVideoOutput(boost::asio::io_context& io_context,
                                     configuration::IConfiguration::Pointer configuration,
                                     std::shared_ptr<buzz::autoapp::Transport::Transport> transport)
    : VideoOutput(std::move(configuration))
    , transport_(std::move(transport))
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Created (transport-only).";
    this->startWorker();
}

SharedVideoOutput::~SharedVideoOutput()
{
    this->stopWorker();
}

bool SharedVideoOutput::open()
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Opened.";
    return true;
}

bool SharedVideoOutput::init()
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Initialized.";
    return true;
}

void SharedVideoOutput::write(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer)
{
    if (transport_ == nullptr) {
        return;
    }

    if (!workerStarted_.load(std::memory_order_acquire)) {
        this->startWorker();
    }

    if (buffer.cdata == nullptr || buffer.size == 0) {
        return;
    }

    this->enqueueFrame(timestamp, buffer);
}

void SharedVideoOutput::stop()
{
    OPENAUTO_LOG(info) << "[SharedVideoOutput] Stopped.";
    this->stopWorker();
}

void SharedVideoOutput::startWorker()
{
    if (transport_ == nullptr) {
        return;
    }

    bool expected = false;
    if (!workerStarted_.compare_exchange_strong(expected, true)) {
        return;
    }

    shuttingDown_.store(false, std::memory_order_release);
    workerThread_ = std::thread([this]() { this->workerLoop(); });
}

void SharedVideoOutput::stopWorker()
{
    if (!workerStarted_.load(std::memory_order_acquire)) {
        return;
    }

    shuttingDown_.store(true, std::memory_order_release);
    queueCv_.notify_all();

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    frameQueue_.clear();
    workerStarted_.store(false, std::memory_order_release);
}

void SharedVideoOutput::workerLoop()
{
    for (;;) {
        Frame frame;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this]() {
                return shuttingDown_.load(std::memory_order_acquire) || !frameQueue_.empty();
            });

            if (shuttingDown_.load(std::memory_order_acquire) && frameQueue_.empty()) {
                break;
            }

            frame = std::move(frameQueue_.front());
            frameQueue_.pop_front();
        }

        if (!transport_ || frame.data.empty()) {
            continue;
        }

        transport_->send(buzz::wire::MsgType::VIDEO,
                         frame.timestamp,
                         frame.data.data(),
                         frame.data.size());

        OPENAUTO_LOG(info) << "[SharedVideoOutput] Wrote chunk with timestamp " << frame.timestamp
                           << " and size " << frame.data.size();
    }
}

void SharedVideoOutput::enqueueFrame(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer)
{
    Frame frame;
    frame.timestamp = timestamp;
    frame.data.assign(static_cast<const uint8_t*>(buffer.cdata),
                      static_cast<const uint8_t*>(buffer.cdata) + buffer.size);

    bool droppedOldest = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (frameQueue_.size() >= kMaxQueueDepth) {
            frameQueue_.pop_front();
            droppedOldest = true;
        }
        frameQueue_.emplace_back(std::move(frame));
    }

    if (droppedOldest) {
        const auto drops = droppedFrames_.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((drops & kQueueDropLogMask) == 0) {
            OPENAUTO_LOG(warning) << "[SharedVideoOutput] Dropped video frame due to stalled consumer. dropCount="
                                  << drops;
        }
    }

    queueCv_.notify_one();
}

}
}
}
}