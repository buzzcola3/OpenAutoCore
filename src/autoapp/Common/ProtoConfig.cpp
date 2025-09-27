#include <f1x/openauto/autoapp/Common/ProtoConfig.hpp>
#include <f1x/openauto/Common/Log.hpp>

#include <fstream>
#include <sstream>
#include <memory>

#include <google/protobuf/text_format.h>
#include <google/protobuf/message.h>
#include <google/protobuf/io/tokenizer.h>              // io::ErrorCollector + ColumnNumber
#include <google/protobuf/io/zero_copy_stream_impl.h>  // io::IstreamInputStream

namespace f1x::openauto::autoapp::config {

namespace {
class CollectingErrorCollector : public google::protobuf::io::ErrorCollector {
public:
  void RecordError(int line,
                   google::protobuf::io::ColumnNumber column,
                   absl::string_view message) override {
    std::ostringstream os;
    os << "line " << line << ", col " << static_cast<int>(column) << ": "
       << std::string(message) << "\n";
    errors_ += os.str();
  }
  void RecordWarning(int line,
                     google::protobuf::io::ColumnNumber column,
                     absl::string_view message) override {
    std::ostringstream os;
    os << "line " << line << ", col " << static_cast<int>(column) << " (warning): "
       << std::string(message) << "\n";
    errors_ += os.str();
  }

  const std::string& errors() const { return errors_; }
private:
  std::string errors_;
};
} // namespace

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
  CollectingErrorCollector ec;
  parser.RecordErrorsTo(&ec);

  std::unique_ptr<google::protobuf::Message> tmp(dst.New());
  if (!tmp) {
    OPENAUTO_LOG(error) << "[ProtoConfig] Failed to create message instance for "
                        << (label ? label : "<unknown>");
    return false;
  }

  if (!parser.Parse(&zcin, tmp.get())) {
    OPENAUTO_LOG(error) << "[ProtoConfig] Parse failed for "
                        << (label ? label : "<unknown>")
                        << " at " << path << ":\n" << ec.errors();
    return false;
  }

  // Copy parsed message into destination.
  dst.CopyFrom(*tmp);
  OPENAUTO_LOG(info) << "[ProtoConfig] Loaded " << (label ? label : "config")
                     << " from " << path;
  return true;
}

} // namespace f1x::openauto::autoapp::config