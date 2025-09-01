/*
 *  This file is part of OpenAutoCore project.
 *  Copyright (C) 2025 buzzcola3 (Samuel Betak)
 *
 *  OpenAutoCore is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenAutoCore is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.
 */

#include "buzz/autoapp/Projection/SharedMemoryProducer.hpp"
#include "f1x/openauto/Common/Log.hpp"
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>

SharedMemoryProducer::SharedMemoryProducer(const std::string& shmName, const std::string& semName, size_t shmSize, size_t maxQueueSize)
    : shmName_(shmName),
      semName_(semName),
      shmSize_(shmSize),
      maxQueueSize_(maxQueueSize),
      semaphore_(SEM_FAILED),
      shm_fd_(-1),
      ptr_(MAP_FAILED),
      stopWorker_(false),
      workerActive_(false)
{
}

SharedMemoryProducer::~SharedMemoryProducer() {
    stopWorker();
    OPENAUTO_LOG(error) << "Producer shutting down and cleaning up resources..." << std::endl;
    if (ptr_ != MAP_FAILED) munmap(ptr_, shmSize_);
    if (shm_fd_ != -1) close(shm_fd_);
    if (semaphore_ != SEM_FAILED) sem_close(semaphore_);
    shm_unlink(shmName_.c_str());
    sem_unlink(semName_.c_str());
}

bool SharedMemoryProducer::init() {
    semaphore_ = sem_open(semName_.c_str(), O_CREAT, 0666, 0);
    if (semaphore_ == SEM_FAILED) {
        perror("sem_open failed");
        return false;
    }
    shm_fd_ = shm_open(shmName_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd_ == -1) {
        perror("shm_open failed");
        return false;
    }
    if (ftruncate(shm_fd_, shmSize_) == -1) {
        perror("ftruncate failed");
        return false;
    }
    ptr_ = mmap(nullptr, shmSize_, PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (ptr_ == MAP_FAILED) {
        perror("mmap failed");
        return false;
    }
    OPENAUTO_LOG(error) << "Producer initialized successfully." << std::endl;
    return true;
}

void SharedMemoryProducer::startWorker() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (workerActive_) return;
    stopWorker_ = false;
    workerActive_ = true;
    workerThread_ = std::thread(&SharedMemoryProducer::workerLoop, this);
    OPENAUTO_LOG(error) << "Producer worker thread started." << std::endl;
}

void SharedMemoryProducer::stopWorker() {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stopWorker_ = true;
        queueCondVar_.notify_all();
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
        OPENAUTO_LOG(error) << "Producer worker thread stopped." << std::endl;
    }
    workerActive_ = false;
}

void SharedMemoryProducer::workerLoop() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    while (!stopWorker_) {
        queueCondVar_.wait(lock, [this] { return stopWorker_ || !internalQueue_.empty(); });
        while (!internalQueue_.empty() && !stopWorker_) {
            int sem_val = 0;
            if (sem_getvalue(semaphore_, &sem_val) == -1) {
                perror("sem_getvalue failed");
                break;
            }
            if (sem_val == 0 && ptr_ != MAP_FAILED) {
                // Ready to send
                auto buffer_to_send = std::move(internalQueue_.front());
                internalQueue_.pop();
                lock.unlock();

                size_t bytes_to_copy = std::min(buffer_to_send.size(), shmSize_);
                memcpy(ptr_, buffer_to_send.data(), bytes_to_copy);
                if (sem_post(semaphore_) == -1) {
                    perror("sem_post failed");
                } else {
//                    OPENAUTO_LOG(error) << "[Queue] Sent buffer. " << internalQueue_.size() << " remaining." << std::endl;
                }

                lock.lock();
            } else {
                // Not ready, wait a bit before retrying
                queueCondVar_.wait_for(lock, std::chrono::milliseconds(10));
            }
        }
    }
}

void SharedMemoryProducer::writeBuffer(const unsigned char* buffer, size_t size) {
    bool needStartWorker = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (ptr_ == MAP_FAILED) {
            std::cerr << "Producer is not initialized. Cannot write." << std::endl;
            return;
        }
        if (internalQueue_.size() >= maxQueueSize_) {
            std::cerr << "Producer internal queue is full. Dropping buffer." << std::endl;
            return;
        }
        internalQueue_.push(std::vector<unsigned char>(buffer, buffer + size));
//        OPENAUTO_LOG(error) << "[Queue] Added buffer. " << internalQueue_.size() << " in queue." << std::endl;
        needStartWorker = !workerActive_;
    }
    if (needStartWorker) {
        startWorker();
    }
    queueCondVar_.notify_one();
}