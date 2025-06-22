/*
*  This file is part of openauto project.
*  ...
*/

#pragma once

// --- Standard Library Headers ---
#include <functional>
#include <optional>
#include <type_traits>

// --- Third-Party Library Headers ---
#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/system/error_code.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

class SequentialBuffer
{
public:
    using IoContext = boost::asio::io_context;
    using Strand = boost::asio::strand<IoContext::executor_type>;

    explicit SequentialBuffer(IoContext& io_context, size_t capacity = 65536);

    template <typename MutableBufferSequence, typename ReadHandler>
    void async_read_some(const MutableBufferSequence& buffers, ReadHandler&& handler);

    size_t write(const char* data, size_t len);
    void cancel();
    void close();

private:
    using ReadHandlerType = std::function<void(const boost::system::error_code&)>;

    void complete_pending_read(const boost::system::error_code& ec);

    // PIMPL removed. Members are now directly in the class.
    Strand strand_;
    boost::circular_buffer<char> buffer_;
    std::optional<ReadHandlerType> pending_read_handler_;
    bool is_closed_;
};

}
}
}
}

// Include the template implementation at the end of the header.
#include <f1x/openauto/autoapp/Projection/SequentialBuffer.inl>