#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <chrono>
#include "capnproto_shm_transport.hpp"
#include "wire.hpp"
#include <kj/array.h>
#include <capnp/serialize.h>

namespace buzz::autoapp::Transport {

class Transport {
public:
  using Handler = std::function<void(const void*, std::size_t)>;

  enum class Side { Unknown, A, B };

  explicit Transport(std::size_t maxQueue);
  ~Transport();

  void setHandler(Handler handler);

  // Explicit startup as Side A (creator). Returns true on success.
  // poll = initial local poll interval. If clean=true, removes any stale shared memory first.
  bool startAsA(std::chrono::microseconds poll = std::chrono::microseconds{1000},
                bool clean = true);

  // Explicit startup as Side B (opener). wait = max wait for A to appear.
  // Returns true on success.
  bool startAsB(std::chrono::milliseconds wait,
                std::chrono::microseconds poll = std::chrono::microseconds{1000});

  // Send a Cap'n Proto envelope.
  void send(buzz::wire::MsgType msgType,
            uint64_t timestampUsec,
            const void* data,
            size_t size);

  // Optional pull-style receive.
  void receiveAndPrintOnce(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

  uint64_t sentCount() const noexcept { return sendCount_; }
  uint64_t dropCount() const noexcept { return dropCount_; }
  Side side() const noexcept { return side_; }
  bool isRunning() const noexcept { return running_.load(std::memory_order_relaxed); }

  void stop();

private:
  void handleIncomingSlot(const uint8_t* data, uint64_t len);
  void sendFlat(const kj::Array<capnp::word>& flat);

  static constexpr const char* kName = "openauto_core";
  static constexpr std::size_t kSlotSize  = 4096;
  static constexpr std::size_t kSlotCount = 1024;

  std::size_t maxQueue_{};
  std::unique_ptr<uint8_t[]> slotBuf_;
  std::unique_ptr<capnproto_shm_transport::ShmFixedSlotDuplexTransport> shm_;

  std::atomic<bool> running_{false};
  std::atomic<uint64_t> sendCount_{0};
  std::atomic<uint64_t> dropCount_{0};

  Side side_{Side::Unknown};
  Handler handler_;
  std::vector<uint8_t> decodeBuf_;
};

} // namespace buzz::autoapp::Transport