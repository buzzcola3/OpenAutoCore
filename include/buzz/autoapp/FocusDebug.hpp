#pragma once

#include <chrono>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>
#include "buzz/autoapp/DebugGlassMonitor.hpp"
#include "debugglass/widgets/structure.h"
#include "debugglass/widgets/variable.h"

namespace buzz::autoapp::debug {
inline constexpr char kFocusWindowName[] = "AndroidAuto";
inline constexpr char kFocusTabName[] = "Focus";

class FocusDebugPanel {
public:
    void RecordAudioRequest(const std::string& requestSummary) {
        Publish(&audio_request_var_, "Audio request", requestSummary);
    }

    void RecordAudioResponse(const std::string& responseSummary) {
        Publish(&audio_response_var_, "Audio response", responseSummary);
    }

    void RecordVideoRequest(int displayChannelId, const std::string& modeSummary, const std::string& reasonSummary) {
        std::ostringstream details;
        details << "disp=" << displayChannelId << " mode=" << modeSummary << " reason=" << reasonSummary;
        Publish(&video_request_var_, "Video request", details.str());
    }

    void RecordVideoResponse(const std::string& focusSummary, bool unsolicited) {
        std::ostringstream details;
        details << "focus=" << focusSummary << " unsolicited=" << (unsolicited ? "true" : "false");
        Publish(&video_response_var_, "Video response", details.str());
    }

private:
    void Publish(debugglass::Variable** slot, const char* prefix, const std::string& details) {
        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureWidgetsLocked() || slot == nullptr || *slot == nullptr) {
            return;
        }

        (*slot)->SetValue(BuildEntry(prefix, details));
    }

    bool EnsureWidgetsLocked() {
        if (audio_structure_ != nullptr && video_structure_ != nullptr) {
            return true;
        }

        auto& window = gDebugGlassMonitor.windows[kFocusWindowName];
        auto* tab = window.tabs.find(kFocusTabName);
        if (tab == nullptr) {
            tab = &window.tabs.Add(kFocusTabName);
        }

        if (audio_structure_ == nullptr) {
            audio_structure_ = &tab->AddStructure("Audio Focus");
            audio_request_var_ = &audio_structure_->AddVariable("Last Request");
            audio_response_var_ = &audio_structure_->AddVariable("Last Response");
        }

        if (video_structure_ == nullptr) {
            video_structure_ = &tab->AddStructure("Video Focus");
            video_request_var_ = &video_structure_->AddVariable("Last Request");
            video_response_var_ = &video_structure_->AddVariable("Last Response");
        }

        return true;
    }

    std::string BuildEntry(const char* prefix, const std::string& details) {
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&tt, &tm);

        char timestamp[32];
        if (std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &tm) == 0) {
            timestamp[0] = '\0';
        }

        std::ostringstream stream;
        stream << prefix;
        if (timestamp[0] != '\0') {
            stream << " @ " << timestamp;
        }
        stream << " | " << details;
        return stream.str();
    }

    std::mutex mutex_;
    debugglass::Structure* audio_structure_ = nullptr;
    debugglass::Structure* video_structure_ = nullptr;
    debugglass::Variable* audio_request_var_ = nullptr;
    debugglass::Variable* audio_response_var_ = nullptr;
    debugglass::Variable* video_request_var_ = nullptr;
    debugglass::Variable* video_response_var_ = nullptr;
};

inline FocusDebugPanel& GetFocusPanel() {
    static FocusDebugPanel panel;
    return panel;
}

inline void RecordAudioFocusRequest(const std::string& requestSummary) {
    if (!gDebugGlassMonitor.IsRunning()) {
        return;
    }
    GetFocusPanel().RecordAudioRequest(requestSummary);
}

inline void RecordAudioFocusResponse(const std::string& responseSummary) {
    if (!gDebugGlassMonitor.IsRunning()) {
        return;
    }
    GetFocusPanel().RecordAudioResponse(responseSummary);
}

inline void RecordVideoFocusRequest(int displayChannelId, const std::string& modeSummary, const std::string& reasonSummary) {
    if (!gDebugGlassMonitor.IsRunning()) {
        return;
    }
    GetFocusPanel().RecordVideoRequest(displayChannelId, modeSummary, reasonSummary);
}

inline void RecordVideoFocusResponse(const std::string& focusSummary, bool unsolicited) {
    if (!gDebugGlassMonitor.IsRunning()) {
        return;
    }
    GetFocusPanel().RecordVideoResponse(focusSummary, unsolicited);
}

}  // namespace buzz::autoapp::debug
