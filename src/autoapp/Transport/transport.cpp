#include "transport.hpp"
#include "wire.capnp.h"
#include "wire.hpp"

#include <capnp/serialize.h>
#include <capnp/message.h>
#include "capnproto_shm_transport.hpp"

#include <cstring>
#include <iostream>
#include <vector>
#include <chrono>

namespace buzz::autoapp::Transport {

Transport::Transport(std::size_t maxQueue)
  : maxQueue_(maxQueue) {
  slotBuf_ = std::unique_ptr<uint8_t[]>{ new uint8_t[kSlotSize] };
  // Removed auto start; caller must invoke startAsA() or startAsB().
}

Transport::~Transport() {
  stop();
}

bool Transport::startAsA(std::chrono::microseconds poll, bool clean) {
  if (running_.load(std::memory_order_relaxed))
    return side_ == Side::A;

  try {
    if (clean) {
      capnproto_shm_transport::ShmFixedSlotDuplexTransport::remove(kName);
    }
    shm_ = std::make_unique<capnproto_shm_transport::ShmFixedSlotDuplexTransport>(
        kName,
        kSlotSize,
        kSlotCount,
        [this](const uint8_t* data, uint64_t len) { this->handleIncomingSlot(data, len); },
        poll
    );
    running_.store(true, std::memory_order_relaxed);
    side_ = Side::A;
    std::cout << "[Transport] Started as Side A name=" << kName
              << " slotSize=" << kSlotSize
              << " slotCount=" << kSlotCount
              << " poll(us)=" << poll.count() << "\n";
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[Transport] startAsA failed: " << e.what() << "\n";
    running_.store(false, std::memory_order_relaxed);
    side_ = Side::Unknown;
    return false;
  }
}

bool Transport::startAsB(std::chrono::milliseconds wait,
                         std::chrono::microseconds poll) {
  if (running_.load(std::memory_order_relaxed))
    return side_ == Side::B;

  try {
    auto opened = capnproto_shm_transport::ShmFixedSlotDuplexTransport::open(
        kName,
        wait,
        [this](const uint8_t* data, uint64_t len) { this->handleIncomingSlot(data, len); },
        poll
    );
    shm_ = std::make_unique<capnproto_shm_transport::ShmFixedSlotDuplexTransport>(
        std::move(opened));
    running_.store(true, std::memory_order_relaxed);
    side_ = Side::B;
    std::cout << "[Transport] Started as Side B name=" << kName
              << " wait(ms)=" << wait.count()
              << " poll(us)=" << poll.count() << "\n";
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[Transport] startAsB failed: " << e.what() << "\n";
    running_.store(false, std::memory_order_relaxed);
    side_ = Side::Unknown;
    return false;
  }
}

void Transport::stop() {
  if (!running_.exchange(false, std::memory_order_relaxed))
    return;
  shm_.reset();
  side_ = Side::Unknown;
  std::cout << "[Transport] Stopped\n";
}

void Transport::setHandler(Handler handler) {
  handler_ = std::move(handler);
}

void Transport::handleIncomingSlot(const uint8_t* data, uint64_t len) {
  if (len < 4) {
    std::cerr << "[Transport] RX slot too small (" << len << ")\n";
    return;
  }

  uint32_t payloadLen = 0;
  std::memcpy(&payloadLen, data, sizeof(payloadLen));
  if (payloadLen > len - 4) {
    std::cerr << "[Transport] RX length prefix invalid payloadLen=" << payloadLen
              << " slotLen=" << len << "\n";
    return;
  }

  const uint8_t* payload = data + 4;
  bool aligned = ((reinterpret_cast<uintptr_t>(payload) & (alignof(capnp::word) - 1)) == 0);
  const uint8_t* decodePtr = nullptr;

  if (aligned) {
    decodePtr = payload;
  } else {
    decodeBuf_.resize(payloadLen);
    std::memcpy(decodeBuf_.data(), payload, payloadLen);
    decodePtr = decodeBuf_.data();
  }

  if (payloadLen % sizeof(capnp::word) != 0) {
    std::cerr << "[Transport] RX payload not word multiple (" << payloadLen << ")\n";
    return;
  }

  auto wordCount = payloadLen / sizeof(capnp::word);
  auto words = kj::ArrayPtr<const capnp::word>(
      reinterpret_cast<const capnp::word*>(decodePtr), wordCount);

  try {
    capnp::FlatArrayMessageReader reader(words);
    auto env = reader.getRoot<::Envelope>();
    auto msgType = static_cast<uint32_t>(env.getMsgType());
    auto ts = env.getTimestampUsec();
    auto dataSection = env.getData();

    std::cout << "[Transport] RX side=" << (side_ == Side::A ? "A" : (side_ == Side::B ? "B" : "?"))
              << " msgType=" << msgType
              << " ts=" << ts
              << " payloadBytes=" << dataSection.size() << "\n";

    if (handler_) {
      handler_(dataSection.begin(), dataSection.size());
    }
  } catch (const std::exception& e) {
    std::cerr << "[Transport] RX decode error: " << e.what() << "\n";
  }
}

void Transport::receiveAndPrintOnce(std::chrono::milliseconds timeout) {
  if (!running_.load(std::memory_order_relaxed) || !shm_)
    return;

  std::vector<uint8_t> out;
  if (shm_->recvSlot(out, timeout)) {
    if (!out.empty()) {
      handleIncomingSlot(out.data(), out.size());
    }
  }
}

void Transport::sendFlat(const kj::Array<capnp::word>& flat) {
  if (!running_.load(std::memory_order_relaxed) || !shm_) {
    ++dropCount_;
    return;
  }

  auto bytes = flat.asBytes();
  const uint32_t payloadLen = static_cast<uint32_t>(bytes.size());
  constexpr std::size_t headerLen = 4;
  const std::size_t needed = headerLen + payloadLen;

  if (needed > kSlotSize) {
    ++dropCount_;
    if ((dropCount_ & 0xFF) == 0) {
      std::cerr << "[Transport] oversize message " << needed
                << " > slotSize " << kSlotSize << " (drop)\n";
    }
    return;
  }

  uint8_t* slot = slotBuf_.get();
  std::memcpy(slot, &payloadLen, sizeof(payloadLen));
  if (payloadLen) {
    std::memcpy(slot + headerLen, bytes.begin(), payloadLen);
  }

  if (!shm_->sendSlot(slot, kSlotSize, std::chrono::milliseconds{-1})) {
    ++dropCount_;
    if ((dropCount_ & 0xFF) == 0) {
      std::cerr << "[Transport] sendSlot failed (dropCount=" << dropCount_ << ")\n";
    }
    return;
  }

  ++sendCount_;
  if ((sendCount_ & 0xFF) == 0) {
    std::cout << "[Transport] sent=" << sendCount_
              << " lastSize=" << payloadLen
              << " drops=" << dropCount_ << "\n";
  }
}

void Transport::send(buzz::wire::MsgType msgType,
                     uint64_t timestampUsec,
                     const void* data,
                     size_t size) {
  if (!running_.load(std::memory_order_relaxed)) {
    ++dropCount_;
    return;
  }

  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<::Envelope>();
  env.setMsgType(static_cast<::MsgType>(msgType));
  env.setTimestampUsec(timestampUsec);
  auto payload = env.initData(size);
  if (size && data) {
    std::memcpy(payload.begin(), data, size);
  }

  auto flat = capnp::messageToFlatArray(mb);
  sendFlat(flat);
}

} // namespace buzz::autoapp::Transport