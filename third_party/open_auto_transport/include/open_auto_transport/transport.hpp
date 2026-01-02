/*
*  This file is part of OpenAutoCore project.
*  Copyright (C) 2025 buzzcola3 (Samuel Betak)
*
*  OpenAutoCore is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  OpenAutoCore is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <chrono>
#include "wire.hpp"

namespace buzz::autoapp::Transport {

// Thin ABI-stable wrapper that forwards to the libc++-built transport
// implementation via a C bridge. This keeps the libstdc++ binary compatible
// with the prebuilt shared library.
class Transport {
public:
  using Handler = std::function<void(uint64_t, const void*, std::size_t)>;

  enum class Side { Unknown = 0, A, B };

  Transport();
  ~Transport();

  void setHandler(Handler handler);
  void addTypeHandler(buzz::wire::MsgType type, Handler handler);

  bool startAsA(std::chrono::microseconds poll = std::chrono::microseconds{1000},
                bool clean = true);
  bool startAsB(std::chrono::milliseconds wait,
                std::chrono::microseconds poll = std::chrono::microseconds{1000});

  void send(buzz::wire::MsgType msgType,
            uint64_t timestampUsec,
            const void* data,
            size_t size);

  uint64_t sentCount() const noexcept;
  uint64_t dropCount() const noexcept;
  Side side() const noexcept;
  bool isRunning() const noexcept;

  void stop();

private:
  struct HandlerHolder {
    Handler fn;
  };

  std::shared_ptr<HandlerHolder> makeHandlerHolder(Handler handler);
  void keepAlive(std::shared_ptr<HandlerHolder> holder);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace buzz::autoapp::Transport