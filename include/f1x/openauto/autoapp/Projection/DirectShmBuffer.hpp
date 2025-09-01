#pragma once

#include <string>
#include <cstddef>
#include <cstdint>
#include <semaphore.h>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

// A simple, non-circular buffer that writes data directly to shared memory.
class DirectShmBuffer
{
public:
    DirectShmBuffer(const std::string& shm_name, size_t capacity);
    ~DirectShmBuffer();

    // Writes a header (timestamp, size) followed by data.
    // Returns bytes written, or 0 on failure (e.g., not enough space).
    size_t write(uint64_t timestamp, const char* data, size_t len);

    // This method no longer has a meaningful effect without a control block.
    void clear();

private:
    std::string shm_name_;
    std::string semaphore_name_;
    size_t capacity_;
    int shm_fd_;

    char* data_buffer_;
    sem_t* semaphore_;
};

}
}
}
}