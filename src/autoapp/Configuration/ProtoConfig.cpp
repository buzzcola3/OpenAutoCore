#include <f1x/openauto/autoapp/Configuration/ProtoConfig.hpp>
#include <f1x/openauto/Common/Log.hpp>

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