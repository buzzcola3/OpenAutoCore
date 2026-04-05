// Copyright (C) 2025 Samuel Betak (buzzcola3 - buzzcola3@github.com)
//
// This file is part of OpenAutoCore.
//
// OpenAutoCore is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// OpenAutoCore is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OpenAutoCore. If not, see <http://www.gnu.org/licenses/>.

#include <Configuration/ProtoConfig.hpp>
#include <Common/Log.hpp>

#include <fstream>
#include <memory>

#include <google/protobuf/text_format.h>
#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>  // io::IstreamInputStream

namespace f1x::openauto::autoapp::config {

bool loadTextProto(const std::string& path,
                   google::protobuf::Message& dst,
                   const char* label) noexcept {
  std::ifstream in(path);
  if (!in) {
    OPENAUTO_LOG(error) << "[ProtoConfig] Cannot open " << (label ? label : "config")
                        << " file: " << path;
    return false;
  }

  google::protobuf::io::IstreamInputStream zcin(&in);
  google::protobuf::TextFormat::Parser parser;
  std::unique_ptr<google::protobuf::Message> tmp(dst.New());
  if (!tmp) {
    OPENAUTO_LOG(error) << "[ProtoConfig] Failed to create message instance for "
                        << (label ? label : "<unknown>");
    return false;
  }

  if (!parser.Parse(&zcin, tmp.get())) {
    OPENAUTO_LOG(error) << "[ProtoConfig] Parse failed for "
                        << (label ? label : "<unknown>")
                        << " at " << path;
    return false;
  }

  // Copy parsed message into destination.
  dst.CopyFrom(*tmp);
  OPENAUTO_LOG(info) << "[ProtoConfig] Loaded " << (label ? label : "config")
                     << " from " << path;
  return true;
}

} // namespace f1x::openauto::autoapp::config