/*
*  This file is part of openauto project.
*  ...
*/

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace projection
{

template <typename MutableBufferSequence, typename ReadHandler>
void SequentialBuffer::async_read_some(const MutableBufferSequence& buffers, ReadHandler&& handler)
{
    // Post the work to the strand to ensure thread safety.
    boost::asio::post(strand_, [this, buffers, handler = std::forward<ReadHandler>(handler)]() mutable {
        if (pending_read_handler_)
        {
            // Operation already in progress, fail fast.
            boost::system::error_code ec = make_error_code(boost::system::errc::operation_in_progress);
            boost::asio::post(strand_, [handler, ec]() mutable {
                handler(ec, 0);
            });
            return;
        }

        if (!buffer_.empty())
        {
            // Data is available, complete immediately.
            // Create a source buffer sequence from the circular_buffer's contiguous arrays.
            std::array<boost::asio::const_buffer, 2> source_buffers = {
                boost::asio::buffer(buffer_.array_one().first, buffer_.array_one().second),
                boost::asio::buffer(buffer_.array_two().first, buffer_.array_two().second)
            };

            const auto bytes_to_transfer = boost::asio::buffer_copy(buffers, source_buffers);
            buffer_.erase_begin(bytes_to_transfer);

            boost::system::error_code ec;
            boost::asio::post(strand_, [handler, ec, bytes_to_transfer]() mutable {
                handler(ec, bytes_to_transfer);
            });
        }
        else if (is_closed_)
        {
            // Buffer is closed and empty, signal EOF.
            boost::system::error_code ec = make_error_code(boost::asio::error::eof);
            boost::asio::post(strand_, [handler, ec]() mutable {
                handler(ec, 0);
            });
        }
        else
        {
            // No data, store the handler and wait for a write.
            pending_read_handler_ = [this, buffers, handler](const boost::system::error_code& ec) mutable {
                if (ec)
                {
                    handler(ec, 0);
                    return;
                }

                // Create a source buffer sequence from the circular_buffer's contiguous arrays.
                std::array<boost::asio::const_buffer, 2> source_buffers = {
                    boost::asio::buffer(buffer_.array_one().first, buffer_.array_one().second),
                    boost::asio::buffer(buffer_.array_two().first, buffer_.array_two().second)
                };

                const auto bytes_to_transfer = boost::asio::buffer_copy(buffers, source_buffers);
                buffer_.erase_begin(bytes_to_transfer);
                handler(boost::system::error_code(), bytes_to_transfer);
            };
        }
    });
}

}
}
}
}