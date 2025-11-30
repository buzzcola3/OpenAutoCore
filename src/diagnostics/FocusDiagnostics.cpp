#include "buzz/autoapp/FocusDiagnostics.hpp"

#include <chrono>
#include <ctime>
#include <mutex>
#include <sstream>
#include <string>

#include "buzz/autoapp/DebugGlassMonitor.hpp"
#include "debugglass/debugglass.h"
#include "debugglass/widgets/message_monitor.h"
#include "debugglass/widgets/structure.h"
#include "debugglass/widgets/variable.h"

namespace buzz::autoapp {
namespace {

std::string FormatTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
    localtime_r(&tt, &tm);
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &tm) == 0) {
        return {};
    }
    return buffer;
}

class FocusDiagnosticsPanel {
public:
    static FocusDiagnosticsPanel& Instance() {
        static FocusDiagnosticsPanel instance;
        return instance;
    }

    void RecordAudioRequest(aap_protobuf::service::control::message::AudioFocusRequestType type) {
        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureAudioWidgetsLocked()) {
            return;
        }

        const std::string name = aap_protobuf::service::control::message::AudioFocusRequestType_Name(type);
        lastAudioRequestVar_->SetValue(name);
        lastAudioUpdateVar_->SetValue(FormatTimestamp());
        audioEventsMonitor_->UpsertMessage(NextAudioEventId(), "REQ: " + name);
    }

    void RecordAudioResponse(aap_protobuf::service::control::message::AudioFocusStateType state) {
        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureAudioWidgetsLocked()) {
            return;
        }

        const std::string name = aap_protobuf::service::control::message::AudioFocusStateType_Name(state);
        lastAudioResponseVar_->SetValue(name);
        lastAudioUpdateVar_->SetValue(FormatTimestamp());
        audioEventsMonitor_->UpsertMessage(NextAudioEventId(), "RES: " + name);
    }

    void RecordVideoRequest(aap_protobuf::service::media::video::message::VideoFocusMode mode,
                            aap_protobuf::service::media::video::message::VideoFocusReason reason) {
        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureVideoWidgetsLocked()) {
            return;
        }

        const std::string mode_name = aap_protobuf::service::media::video::message::VideoFocusMode_Name(mode);
        const std::string reason_name = aap_protobuf::service::media::video::message::VideoFocusReason_Name(reason);
        lastVideoRequestModeVar_->SetValue(mode_name);
        lastVideoRequestReasonVar_->SetValue(reason_name);
        lastVideoUpdateVar_->SetValue(FormatTimestamp());
        videoEventsMonitor_->UpsertMessage(NextVideoEventId(), "REQ: " + mode_name + " reason=" + reason_name);
    }

    void RecordVideoNotification(aap_protobuf::service::media::video::message::VideoFocusMode focus,
                                 bool unsolicited) {
        if (!gDebugGlassMonitor.IsRunning()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!EnsureVideoWidgetsLocked()) {
            return;
        }

        const std::string focus_name = aap_protobuf::service::media::video::message::VideoFocusMode_Name(focus);
        lastVideoNotificationVar_->SetValue(focus_name);
        lastVideoUnsolicitedVar_->SetValue(unsolicited ? "true" : "false");
        lastVideoUpdateVar_->SetValue(FormatTimestamp());
        videoEventsMonitor_->UpsertMessage(NextVideoEventId(),
                                           std::string("IND: ") + focus_name + (unsolicited ? " (unsolicited)" : ""));
    }

private:
    bool EnsureAudioWidgetsLocked() {
        if (lastAudioRequestVar_ != nullptr) {
            return true;
        }

        auto* window = gDebugGlassMonitor.windows.find("Focus");
        if (window == nullptr) {
            window = &gDebugGlassMonitor.windows.Add("Focus");
        }

        auto* tab = window->tabs.find("Audio");
        if (tab == nullptr) {
            tab = &window->tabs.Add("Audio");
        }

        audioStructure_ = &tab->AddStructure("Audio Focus");
        lastAudioRequestVar_ = &audioStructure_->AddVariable("Last Request");
        lastAudioResponseVar_ = &audioStructure_->AddVariable("Last Response");
        lastAudioUpdateVar_ = &audioStructure_->AddVariable("Last Update");
        audioEventsMonitor_ = &tab->AddMessageMonitor("Audio Events");
        return true;
    }

    bool EnsureVideoWidgetsLocked() {
        if (lastVideoRequestModeVar_ != nullptr) {
            return true;
        }

        auto* window = gDebugGlassMonitor.windows.find("Focus");
        if (window == nullptr) {
            window = &gDebugGlassMonitor.windows.Add("Focus");
        }

        auto* tab = window->tabs.find("Video");
        if (tab == nullptr) {
            tab = &window->tabs.Add("Video");
        }

        videoStructure_ = &tab->AddStructure("Video Focus");
        lastVideoRequestModeVar_ = &videoStructure_->AddVariable("Last Request Mode");
        lastVideoRequestReasonVar_ = &videoStructure_->AddVariable("Last Request Reason");
        lastVideoNotificationVar_ = &videoStructure_->AddVariable("Last Notification");
        lastVideoUnsolicitedVar_ = &videoStructure_->AddVariable("Notification Unsolicited");
        lastVideoUpdateVar_ = &videoStructure_->AddVariable("Last Update");
        videoEventsMonitor_ = &tab->AddMessageMonitor("Video Events");
        return true;
    }

    std::string NextAudioEventId() {
        return "audio-" + std::to_string(++audioEventCounter_);
    }

    std::string NextVideoEventId() {
        return "video-" + std::to_string(++videoEventCounter_);
    }

    std::mutex mutex_;
    uint64_t audioEventCounter_ = 0;
    uint64_t videoEventCounter_ = 0;

    debugglass::Structure* audioStructure_ = nullptr;
    debugglass::Variable* lastAudioRequestVar_ = nullptr;
    debugglass::Variable* lastAudioResponseVar_ = nullptr;
    debugglass::Variable* lastAudioUpdateVar_ = nullptr;
    debugglass::MessageMonitor* audioEventsMonitor_ = nullptr;

    debugglass::Structure* videoStructure_ = nullptr;
    debugglass::Variable* lastVideoRequestModeVar_ = nullptr;
    debugglass::Variable* lastVideoRequestReasonVar_ = nullptr;
    debugglass::Variable* lastVideoNotificationVar_ = nullptr;
    debugglass::Variable* lastVideoUnsolicitedVar_ = nullptr;
    debugglass::Variable* lastVideoUpdateVar_ = nullptr;
    debugglass::MessageMonitor* videoEventsMonitor_ = nullptr;
};

}  // namespace

void RecordAudioFocusRequest(aap_protobuf::service::control::message::AudioFocusRequestType type) {
    FocusDiagnosticsPanel::Instance().RecordAudioRequest(type);
}

void RecordAudioFocusResponse(aap_protobuf::service::control::message::AudioFocusStateType state) {
    FocusDiagnosticsPanel::Instance().RecordAudioResponse(state);
}

void RecordVideoFocusRequest(aap_protobuf::service::media::video::message::VideoFocusMode mode,
                             aap_protobuf::service::media::video::message::VideoFocusReason reason) {
    FocusDiagnosticsPanel::Instance().RecordVideoRequest(mode, reason);
}

void RecordVideoFocusNotification(aap_protobuf::service::media::video::message::VideoFocusMode focus,
                                  bool unsolicited) {
    FocusDiagnosticsPanel::Instance().RecordVideoNotification(focus, unsolicited);
}

}  // namespace buzz::autoapp
