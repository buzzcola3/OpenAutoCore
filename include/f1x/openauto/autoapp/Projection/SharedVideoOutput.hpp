/*
*  This file is part of openauto project.
*  Copyright (C) 2025 buzzcola3 (Samuel Betak)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <f1x/openauto/autoapp/Projection/VideoOutput.hpp>
#include <cstdint>
#include <boost/asio/io_context.hpp>
#include <memory>

// Forward-declare Transport to avoid heavy includes in header.
namespace buzz { namespace autoapp { namespace Transport { class Transport; } } }

namespace f1x { namespace openauto { namespace autoapp { namespace projection {
constexpr size_t MAX_VIDEO_CHUNK_SIZE = 1920 * 1080 * 3;

class SharedVideoOutput : public VideoOutput {
 public:
  SharedVideoOutput(boost::asio::io_context& io_context,
                    configuration::IConfiguration::Pointer configuration,
                    std::shared_ptr<buzz::autoapp::Transport::Transport> transport);
  ~SharedVideoOutput() override = default;

  bool open() override;
  bool init() override;
  void write(uint64_t timestamp, const aasdk::common::DataConstBuffer& buffer) override;
  void stop() override;

 private:
  std::shared_ptr<buzz::autoapp::Transport::Transport> transport_;
};

}}}}