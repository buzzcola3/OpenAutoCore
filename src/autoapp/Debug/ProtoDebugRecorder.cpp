#include "buzz/autoapp/ProtoDebugRecorder.hpp"

#include <google/protobuf/message.h>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#include "buzz/autoapp/DebugGlassMonitor.hpp"
#include "debugglass/widgets/message_monitor.h"
#include "debugglass/widgets/structure.h"

namespace buzz::autoapp::debug {
namespace {

constexpr std::size_t kMaxEntriesPerChannel = 75;
constexpr char kTabName[] = "Initial Messages";
constexpr char kMonitorTitle[] = "First 75 Protobuf DebugStrings";
constexpr char kWindowPrefix[] = "Proto - ";

std::string BuildWindowName(const std::string& channel) {
  std::string name(kWindowPrefix);
  name.append(channel);
  return name;
}

class ProtoDebugRecorder {
 public:
  void Record(const std::string& channel,
              const std::string& label,
              const google::protobuf::Message& message) {
    if (!gDebugGlassMonitor.IsRunning()) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& recorder = recorders_[channel];
    if (recorder.recorded >= kMaxEntriesPerChannel) {
      return;
    }

    if (!EnsureMonitorLocked(channel, recorder)) {
      return;
    }

    ++recorder.recorded;
    std::ostringstream contents;
    contents << label;
    contents << " (" << message.GetTypeName() << ")\n";
    contents << message.DebugString();

    const std::string key = "#" + std::to_string(recorder.recorded);
    recorder.monitor->UpsertMessage(key, contents.str());
  }

  bool HasCapacity(const std::string& channel) {
    if (!gDebugGlassMonitor.IsRunning()) {
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return recorders_[channel].recorded < kMaxEntriesPerChannel;
  }

 private:
  struct ChannelRecorder {
    debugglass::MessageMonitor* monitor = nullptr;
    std::size_t recorded = 0;
  };

  bool EnsureMonitorLocked(const std::string& channel, ChannelRecorder& recorder) {
    if (recorder.monitor != nullptr) {
      return true;
    }

    auto& window = gDebugGlassMonitor.windows[BuildWindowName(channel)];
    auto* tab = window.tabs.find(kTabName);
    if (tab == nullptr) {
      tab = &window.tabs.Add(kTabName);
    }

    auto* existing = tab->FindMessageMonitor(kMonitorTitle);
    if (existing != nullptr) {
      recorder.monitor = const_cast<debugglass::MessageMonitor*>(existing);
    } else {
      recorder.monitor = &tab->AddMessageMonitor(kMonitorTitle);
    }

    return recorder.monitor != nullptr;
  }

  std::mutex mutex_;
  std::unordered_map<std::string, ChannelRecorder> recorders_;
};

ProtoDebugRecorder& GetRecorder() {
  static ProtoDebugRecorder recorder;
  return recorder;
}

}  // namespace

void RecordProtoMessage(const std::string& channel,
                        const std::string& label,
                        const google::protobuf::Message& message) {
  GetRecorder().Record(channel, label, message);
}

bool ShouldTraceProtoMessages(const std::string& channel) {
  return GetRecorder().HasCapacity(channel);
}

}  // namespace buzz::autoapp::debug
