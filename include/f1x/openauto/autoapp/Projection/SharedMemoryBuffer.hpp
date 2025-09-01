#pragma once

#include <boost/asio.hpp>
#include <string>
#include <atomic>
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

class SharedMemoryBuffer
{
public:
    using IoContext = boost::asio::io_context;
    using Strand = boost::asio::strand<IoContext::executor_type>;

    SharedMemoryBuffer(IoContext& io_context, const std::string& shm_name = "/openauto_audio_shm", size_t capacity = 65536);
    ~SharedMemoryBuffer();

    size_t write(uint64_t timestamp, const char* data, size_t len);
    size_t read(char* out, size_t len);

    void close();
    void cancel();

    size_t size() const;
    size_t capacity() const;

private:
    Strand strand_;
    std::string shm_name_;
    int shm_fd_;
    char* buffer_;
    size_t capacity_;
    std::atomic<size_t> write_offset_;
    std::atomic<size_t> read_offset_;
    bool is_closed_;
    sem_t* semaphore_; // <-- Add the semaphore pointer
    std::string semaphore_name_; // <-- Add name for the semaphore
};

}
}
}
}