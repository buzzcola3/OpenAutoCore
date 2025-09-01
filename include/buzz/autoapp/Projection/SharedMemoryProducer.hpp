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

#ifndef SHARED_MEMORY_PRODUCER_HPP
#define SHARED_MEMORY_PRODUCER_HPP

#include <string>
#include <semaphore.h>
#include <queue>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

class SharedMemoryProducer {
public:
    // Add maxQueueSize to prevent unbounded memory usage
    SharedMemoryProducer(const std::string& shmName, const std::string& semName, size_t shmSize, size_t maxQueueSize = 100);
    ~SharedMemoryProducer();

    // Initializes the shared memory and semaphore. Returns true on success.
    bool init();

    // Writes a raw buffer to the shared memory and signals the consumer.
    void writeBuffer(const unsigned char* buffer, size_t size);

private:
    void startWorker();
    void stopWorker();
    void workerLoop();
    void processQueue();

    const std::string shmName_;
    const std::string semName_;
    const size_t shmSize_;
    const size_t maxQueueSize_;

    sem_t* semaphore_;
    int shm_fd_;
    void* ptr_;

    // Thread-safe queue
    std::queue<std::vector<unsigned char>> internalQueue_;
    std::mutex queueMutex_;
    std::condition_variable queueCondVar_;
    bool workerActive_;

    // Worker thread members
    std::thread workerThread_;
    std::atomic<bool> stopWorker_;
};

#endif // SHARED_MEMORY_PRODUCER_HPP