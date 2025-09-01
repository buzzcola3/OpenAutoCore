#include <f1x/openauto/autoapp/Projection/DirectShmBuffer.hpp>
#include <f1x/openauto/Common/Log.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

DirectShmBuffer::DirectShmBuffer(const std::string& shm_name, size_t capacity)
    : shm_name_(shm_name)
    , semaphore_name_("sem.openauto_audio_shm_sem")
    , capacity_(capacity)
    , shm_fd_(-1)
    , data_buffer_(nullptr)
    , semaphore_(SEM_FAILED)
{
    // The total size is just the data capacity.
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd_ == -1) throw std::runtime_error("shm_open failed");

    if (ftruncate(shm_fd_, capacity_) == -1) throw std::runtime_error("ftruncate failed");

    // Map the memory region.
    void* shm_base = mmap(nullptr, capacity_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (shm_base == MAP_FAILED) throw std::runtime_error("mmap failed");

    data_buffer_ = static_cast<char*>(shm_base);

    // Initialize semaphore
    semaphore_ = sem_open(semaphore_name_.c_str(), O_CREAT, 0666, 0);
    if (semaphore_ == SEM_FAILED) throw std::runtime_error("sem_open failed");
}

DirectShmBuffer::~DirectShmBuffer()
{
    if (data_buffer_) {
        munmap(data_buffer_, capacity_);
    }
    if (shm_fd_ != -1) {
        ::close(shm_fd_);
        shm_unlink(shm_name_.c_str());
    }
    if (semaphore_ != SEM_FAILED) {
        sem_close(semaphore_);
        sem_unlink(semaphore_name_.c_str());
    }
}

size_t DirectShmBuffer::write(uint64_t timestamp, const char* data, size_t len)
{
    const size_t header_size = sizeof(uint64_t) + sizeof(uint32_t);
    const size_t total_size = header_size + len;

    // Check if the chunk fits in the buffer at all.
    if (total_size > capacity_) {
        OPENAUTO_LOG(error) << "[DirectShmBuffer] Chunk too large for buffer. "
                            << "Required: " << total_size << ", Capacity: " << capacity_;
        return 0;
    }

    // Always write at the beginning of the data buffer.
    char* write_pos = data_buffer_;

    // 1. Write the 64-bit timestamp
    std::memcpy(write_pos, &timestamp, sizeof(uint64_t));

    // 2. Write the 32-bit size header
    uint32_t chunk_size = static_cast<uint32_t>(len);
    std::memcpy(write_pos + sizeof(uint64_t), &chunk_size, sizeof(uint32_t));

    // 3. Write the actual data payload
    std::memcpy(write_pos + header_size, data, len);

    // 4. Signal the consumer
    sem_post(semaphore_);
    
    return len;
}

void DirectShmBuffer::clear()
{
    // Without a control block, there are no offsets to clear.
    // This method can be left empty.
}

}
}
}
}