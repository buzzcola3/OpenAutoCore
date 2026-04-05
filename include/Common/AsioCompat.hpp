#pragma once
// Compatibility layer for environments where Boost.Asio no longer exposes
// the deprecated io_service API. This re-introduces minimal shims so the
// existing code can keep using io_service/strand/work without wider changes.

#ifndef BOOST_ASIO_NO_DEPRECATED
#define BOOST_ASIO_NO_DEPRECATED 1
#endif

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <chrono>
#include <utility>

#if !defined(ASIO_COMPAT_HAS_IO_SERVICE)
namespace boost {
namespace asio {

class io_service : public io_context {
public:
  using executor_type = io_context::executor_type;

  class strand : public boost::asio::strand<executor_type> {
  public:
    explicit strand(io_context &ctx)
        : boost::asio::strand<executor_type>(ctx.get_executor()) {}
    explicit strand(executor_type exec)
        : boost::asio::strand<executor_type>(std::move(exec)) {}

    template <typename Handler>
    void post(Handler &&handler) const {
          const auto& executor = static_cast<const boost::asio::strand<executor_type>&>(*this);
          boost::asio::post(executor, std::forward<Handler>(handler));
    }

    template <typename Handler>
    void dispatch(Handler &&handler) const {
          const auto& executor = static_cast<const boost::asio::strand<executor_type>&>(*this);
          boost::asio::dispatch(executor, std::forward<Handler>(handler));
    }

        template <typename Handler>
        auto wrap(Handler &&handler) const {
          const auto& executor = static_cast<const boost::asio::strand<executor_type>&>(*this);
          return boost::asio::bind_executor(executor, std::forward<Handler>(handler));
        }

        io_service& get_io_service() const {
          return static_cast<io_service&>(const_cast<boost::asio::execution_context&>(this->context()));
        }
  };

  class work : public executor_work_guard<executor_type> {
  public:
    explicit work(io_context &ctx)
        : executor_work_guard<executor_type>(ctx.get_executor()) {}
  };

  explicit io_service(int concurrency_hint = BOOST_ASIO_CONCURRENCY_HINT_DEFAULT)
      : io_context(concurrency_hint) {}

  using io_context::poll;
  using io_context::poll_one;
  using io_context::run;
  using io_context::run_one;
  using io_context::stop;
  using io_context::restart;

  template <typename Handler>
  void post(Handler &&handler) {
    boost::asio::post(this->get_executor(), std::forward<Handler>(handler));
  }

  template <typename Handler>
  void dispatch(Handler &&handler) {
    boost::asio::dispatch(this->get_executor(), std::forward<Handler>(handler));
  }
};

    class deadline_timer {
    public:
      using clock_type = boost::asio::steady_timer::clock_type;
      using duration_type = boost::asio::steady_timer::duration;
      using time_type = boost::asio::steady_timer::time_point;

      explicit deadline_timer(io_context &ctx) : timer_(ctx) {}

      template <typename Duration>
      void expires_from_now(const Duration &duration) {
        const auto ms = duration.total_milliseconds();
        timer_.expires_after(std::chrono::milliseconds(ms));
      }

      template <typename Handler>
      void async_wait(Handler &&handler) {
        timer_.async_wait(std::forward<Handler>(handler));
      }

      std::size_t cancel() { return timer_.cancel(); }

    private:
      boost::asio::steady_timer timer_;
    };

}  // namespace asio
}  // namespace boost
#define ASIO_COMPAT_HAS_IO_SERVICE 1
#endif  // !defined(ASIO_COMPAT_HAS_IO_SERVICE)
