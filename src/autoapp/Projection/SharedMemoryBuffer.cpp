#include <f1x/openauto/autoapp/Projection/SharedMemoryBuffer.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <f1x/openauto/Common/Log.hpp>
#include <semaphore.h>
#include <cstdint> // For uint32_t

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SharedMemoryBuffer::SharedMemoryBuffer(IoContext& io_context, const std::string& shm_name, size_t capacity)
    : strand_(io_context.get_executor())
    , shm_name_(shm_name)
    , capacity_(capacity)
    , is_closed_(false)
    , semaphore_name_(shm_name + "_sem") // Create a unique semaphore name
{
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm_fd_ == -1) throw std::runtime_error("shm_open failed");

    if (ftruncate(shm_fd_, capacity_) == -1) throw std::runtime_error("ftruncate failed");

    buffer_ = static_cast<char*>(mmap(nullptr, capacity_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0));
    if (buffer_ == MAP_FAILED) throw std::runtime_error("mmap failed");

    write_offset_ = 0;
    read_offset_ = 0;

    // Create or open the named semaphore. Initialize it to 0.
    // The consumer will wait until this value is > 0.
    semaphore_ = sem_open(semaphore_name_.c_str(), O_CREAT, 0666, 0);
    if (semaphore_ == SEM_FAILED)
    {
        throw std::runtime_error("sem_open failed");
    }
}

SharedMemoryBuffer::~SharedMemoryBuffer()
{
    munmap(buffer_, capacity_);
    ::close(shm_fd_);
    shm_unlink(shm_name_.c_str());

    // Close and unlink the semaphore
    sem_close(semaphore_);
    sem_unlink(semaphore_name_.c_str());
}

size_t SharedMemoryBuffer::write(uint64_t timestamp, const char* data, size_t len)
{
    if (is_closed_ || len == 0) return 0;

    const size_t timestamp_size = sizeof(uint64_t);
    const size_t payload_size_len = sizeof(uint32_t);
    const size_t header_size = timestamp_size + payload_size_len;
    const size_t total_size = header_size + len;

        OPENAUTO_LOG(info) << "Timestamp: " << timestamp
                       << ", Data Length: " << len
                       << ", Total Size Written: " << total_size;
                       
    size_t space = capacity_ - write_offset_.load();
    if (space < total_size)
    {
        OPENAUTO_LOG(error) << "[SharedMemoryBuffer] Not enough space for chunk. "
                            << "Required: " << total_size << ", Available: " << space;
        return 0;
    }



    // Get the current write position
    char* write_pos = buffer_ + write_offset_.load();

    // 1. Write the 64-bit timestamp
    std::memcpy(write_pos, &timestamp, timestamp_size);

    // 2. Write the 32-bit size header immediately after the timestamp
    uint32_t chunk_size = static_cast<uint32_t>(len);
    std::memcpy(write_pos + timestamp_size, &chunk_size, payload_size_len);

    // 3. Write the actual data payload after the full header
    std::memcpy(write_pos + header_size, data, len);

    // 4. Update the write offset by the total size written
    write_offset_ += total_size;

    // Signal the consumer that a new chunk is available.
    sem_post(semaphore_);
    
    return len; // Return the size of the data payload written.
}

size_t SharedMemoryBuffer::read(char* out, size_t len)
{
    // IMPORTANT: This read method is now incorrect for a consumer.
    // A consumer must first read 4 bytes to get the chunk size,
    // then read that many bytes for the payload.
    size_t available = write_offset_.load() - read_offset_.load();
    size_t to_read = std::min(len, available);
    if (to_read > 0)
    {
        std::memcpy(out, buffer_ + read_offset_.load(), to_read);
        read_offset_ += to_read;
    }
    return to_read;
}

void SharedMemoryBuffer::close()
{
    is_closed_ = true;
}

void SharedMemoryBuffer::cancel()
{
    // Not implemented
}

size_t SharedMemoryBuffer::size() const
{
    return write_offset_.load() - read_offset_.load();
}

size_t SharedMemoryBuffer::capacity() const
{
    return capacity_;
}

}
}
}
}