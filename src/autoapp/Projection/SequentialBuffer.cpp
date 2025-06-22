/*
*  This file is part of openauto project.
*  ...
*/

#include <f1x/openauto/autoapp/Projection/SequentialBuffer.hpp>
#include <string>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

SequentialBuffer::SequentialBuffer(IoContext& io_context, size_t capacity)
    : strand_(io_context.get_executor())
    , buffer_(capacity)
    , is_closed_(false)
{
}

size_t SequentialBuffer::write(const char* data, size_t len)
{
    if (is_closed_ || len == 0)
    {
        return 0;
    }

    // Post the write operation to the strand to ensure thread safety.
    boost::asio::post(strand_, [this, data_str = std::string(data, len)]() {
        buffer_.insert(buffer_.end(), data_str.begin(), data_str.end());
        complete_pending_read(boost::system::error_code());
    });

    return len;
}

void SequentialBuffer::cancel()
{
    boost::asio::post(strand_, [this]() {
        complete_pending_read(boost::asio::error::operation_aborted);
    });
}

void SequentialBuffer::close()
{
    boost::asio::post(strand_, [this]() {
        if (!is_closed_)
        {
            is_closed_ = true;
            complete_pending_read(boost::asio::error::eof);
        }
    });
}

void SequentialBuffer::complete_pending_read(const boost::system::error_code& ec)
{
    if (pending_read_handler_)
    {
        auto handler = std::move(*pending_read_handler_);
        pending_read_handler_.reset();
        // Post the completion handler back to the strand.
        boost::asio::post(strand_, [handler, ec]() {
            handler(ec);
        });
    }
}

}
}
}
}