#include <transport.hpp>
#include <capnp/serialize.h>
#include "wire.capnp.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace buzz::autoapp::Transport {

Transport::Transport(std::size_t maxQueue)
  : maxQueue_(maxQueue) {
  start();
}

Transport::~Transport() {
  stop();
}

void Transport::start() { running_.store(true, std::memory_order_relaxed); }
void Transport::stop() { running_.store(false, std::memory_order_relaxed); }
void Transport::setHandler(Handler handler) { handler_ = std::move(handler); }

static void printHex(const kj::ArrayPtr<const kj::byte>& bytes, std::size_t maxShow = 64) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  const auto n = std::min(maxShow, bytes.size());
  for (std::size_t i = 0; i < n; ++i) {
    oss << std::setw(2) << static_cast<unsigned>(bytes[i]);
    if (i + 1 != n) oss << ' ';
  }
  if (bytes.size() > n) oss << " ...";
  std::cout << "[Transport] capnp message: " << bytes.size()
            << " bytes, hex[" << n << "]: " << oss.str() << std::dec << std::endl;
}

void Transport::sendWords(MessageBuffer words) {
  if (!running_.load(std::memory_order_relaxed)) {
    std::cout << "[Transport] not running, dropping message of "
              << words.byteSize() << " bytes" << std::endl;
    return;
  }
  printHex(words.asBytes());
}

void Transport::send(uint16_t msgType, uint64_t timestampUsec, const void* data, size_t size) {
  if (!running_.load(std::memory_order_relaxed)) {
    std::cout << "[Transport] not running, dropping envelope of " << size << " bytes\n";
    return;
  }

  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<::Envelope>();
  env.setVersion(1); // keep in sync with wire.capnp's protocolVersion
  env.setMsgType(static_cast<::MsgType>(msgType));
  env.setTimestampUsec(timestampUsec);

  auto payload = env.initData(size);
  if (size > 0 && data != nullptr) {
    std::memcpy(payload.begin(), data, size);
  }

  auto flat = capnp::messageToFlatArray(mb);
  sendWords(MessageBuffer{ kj::mv(flat) });
}

} // namespace buzz::autoapp::Transport