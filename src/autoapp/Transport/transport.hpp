#pragma once

#include <capnp/message.h>
#include <kj/array.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace buzz::autoapp::Transport {

// Flat buffer for Cap'n Proto words.
struct MessageBuffer {
  kj::Array<capnp::word> words;

  MessageBuffer() = default;
  explicit MessageBuffer(kj::Array<capnp::word> w) : words(kj::mv(w)) {}

  kj::ArrayPtr<const kj::byte> asBytes() const { return words.asBytes(); }
  size_t byteSize() const { return asBytes().size(); }
};

class Transport {
public:
  using Handler = std::function<void()>;

  explicit Transport(std::size_t maxQueue);
  ~Transport();

  void start();
  void stop();

  void setHandler(Handler handler);

  // Print a pre-serialized Cap'n Proto message.
  void sendWords(MessageBuffer words);

  // Build Envelope(version, msgType, timestampUsec, data) and print it.
  void send(uint16_t msgType, uint64_t timestampUsec, const void* data, size_t size);
  void send(uint16_t msgType, uint64_t timestampUsec, kj::ArrayPtr<const kj::byte> bytes) {
    send(msgType, timestampUsec, bytes.begin(), bytes.size());
  }

  std::size_t size() const { return 0; }

private:
  const std::size_t maxQueue_;
  std::atomic<bool> running_{false};
  Handler handler_;
};

} // namespace buzz::autoapp::Transport