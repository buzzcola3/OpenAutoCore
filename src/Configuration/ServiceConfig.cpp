#include <Configuration/ServiceConfig.hpp>
#include <Common/Log.hpp>

#include <cstdio>
#include <fstream>

#include <google/protobuf/text_format.h>

// Proto enum headers for _Parse helpers
#include <aap_protobuf/service/control/message/DriverPosition.pb.h>
#include <aap_protobuf/service/control/message/ConnectionConfiguration.pb.h>
#include <aap_protobuf/service/control/message/PingConfiguration.pb.h>
#include <aap_protobuf/service/control/message/HeadUnitInfo.pb.h>
#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/media/sink/MediaSinkService.pb.h>
#include <aap_protobuf/service/media/source/MediaSourceService.pb.h>
#include <aap_protobuf/service/media/shared/message/MediaCodecType.pb.h>
#include <aap_protobuf/service/media/shared/message/AudioConfiguration.pb.h>
#include <aap_protobuf/service/media/sink/message/AudioStreamType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoConfiguration.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoFrameRateType.pb.h>
#include <aap_protobuf/service/sensorsource/SensorSourceService.pb.h>
#include <aap_protobuf/service/sensorsource/message/Sensor.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>
#include <aap_protobuf/service/inputsource/InputSourceService.pb.h>
#include <aap_protobuf/service/bluetooth/BluetoothService.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingMethod.pb.h>

namespace f1x::openauto::autoapp::configuration {

using json = nlohmann::json;
using SDR = aap_protobuf::service::control::message::ServiceDiscoveryResponse;

// ---------------------------------------------------------------------------
// Enum parsing helpers — return error string on failure, empty on success.
// ---------------------------------------------------------------------------

namespace {

using namespace aap_protobuf;

template <typename E, typename ParseFn>
std::string parseEnum(const json& j, const std::string& key, E* out, ParseFn parseFn) {
    if (!j.contains(key)) return {};
    if (!j[key].is_string())
        return "field '" + key + "' must be a string";
    auto name = j[key].get<std::string>();
    if (!parseFn(name, out))
        return "invalid " + key + " value: '" + name + "'";
    return {};
}

#define PARSE_ENUM(j, key, out, EnumType) \
    parseEnum(j, key, out, EnumType##_Parse)

// ---------------------------------------------------------------------------
// Sub-message builders
// ---------------------------------------------------------------------------

std::string buildAudioConfig(
    const json& j,
    service::media::shared::message::AudioConfiguration* out) {
    if (!j.contains("sampling_rate") || !j.contains("number_of_bits") ||
        !j.contains("number_of_channels"))
        return "audio config requires sampling_rate, number_of_bits, number_of_channels";
    out->set_sampling_rate(j["sampling_rate"].get<uint32_t>());
    out->set_number_of_bits(j["number_of_bits"].get<uint32_t>());
    out->set_number_of_channels(j["number_of_channels"].get<uint32_t>());
    return {};
}

std::string buildVideoConfig(
    const json& j,
    service::media::sink::message::VideoConfiguration* out) {
    std::string err;

    if (j.contains("codec_resolution")) {
        service::media::sink::message::VideoCodecResolutionType v;
        err = PARSE_ENUM(j, "codec_resolution", &v,
                         service::media::sink::message::VideoCodecResolutionType);
        if (!err.empty()) return err;
        out->set_codec_resolution(v);
    }
    if (j.contains("frame_rate")) {
        service::media::sink::message::VideoFrameRateType v;
        err = PARSE_ENUM(j, "frame_rate", &v,
                         service::media::sink::message::VideoFrameRateType);
        if (!err.empty()) return err;
        out->set_frame_rate(v);
    }
    if (j.contains("width_margin"))
        out->set_width_margin(j["width_margin"].get<uint32_t>());
    if (j.contains("height_margin"))
        out->set_height_margin(j["height_margin"].get<uint32_t>());
    if (j.contains("density"))
        out->set_density(j["density"].get<uint32_t>());
    if (j.contains("decoder_additional_depth"))
        out->set_decoder_additional_depth(j["decoder_additional_depth"].get<uint32_t>());
    if (j.contains("video_codec_type")) {
        service::media::shared::message::MediaCodecType v;
        err = PARSE_ENUM(j, "video_codec_type", &v,
                         service::media::shared::message::MediaCodecType);
        if (!err.empty()) return err;
        out->set_video_codec_type(v);
    }
    return {};
}

std::string buildMediaSink(const json& j,
                           service::media::sink::MediaSinkService* out) {
    std::string err;

    if (j.contains("available_type")) {
        service::media::shared::message::MediaCodecType v;
        err = PARSE_ENUM(j, "available_type", &v,
                         service::media::shared::message::MediaCodecType);
        if (!err.empty()) return err;
        out->set_available_type(v);
    }
    if (j.contains("audio_type")) {
        service::media::sink::message::AudioStreamType v;
        err = PARSE_ENUM(j, "audio_type", &v,
                         service::media::sink::message::AudioStreamType);
        if (!err.empty()) return err;
        out->set_audio_type(v);
    }
    if (j.contains("audio_configs")) {
        for (auto& ac : j["audio_configs"]) {
            err = buildAudioConfig(ac, out->add_audio_configs());
            if (!err.empty()) return err;
        }
    }
    if (j.contains("video_configs")) {
        for (auto& vc : j["video_configs"]) {
            err = buildVideoConfig(vc, out->add_video_configs());
            if (!err.empty()) return err;
        }
    }
    if (j.contains("available_while_in_call"))
        out->set_available_while_in_call(j["available_while_in_call"].get<bool>());
    return {};
}

std::string buildMediaSource(const json& j,
                             service::media::source::MediaSourceService* out) {
    std::string err;

    if (j.contains("available_type")) {
        service::media::shared::message::MediaCodecType v;
        err = PARSE_ENUM(j, "available_type", &v,
                         service::media::shared::message::MediaCodecType);
        if (!err.empty()) return err;
        out->set_available_type(v);
    }
    if (j.contains("audio_config")) {
        err = buildAudioConfig(j["audio_config"], out->mutable_audio_config());
        if (!err.empty()) return err;
    }
    if (j.contains("available_while_in_call"))
        out->set_available_while_in_call(j["available_while_in_call"].get<bool>());
    return {};
}

std::string buildSensorSource(const json& j,
                              service::sensorsource::SensorSourceService* out) {
    if (j.contains("sensors")) {
        for (auto& s : j["sensors"]) {
            service::sensorsource::message::SensorType st;
            auto err = PARSE_ENUM(s, "sensor_type", &st,
                                  service::sensorsource::message::SensorType);
            if (!err.empty()) return err;
            out->add_sensors()->set_sensor_type(st);
        }
    }
    return {};
}

std::string buildInputSource(const json& j,
                             service::inputsource::InputSourceService* out) {
    if (j.contains("touchscreen")) {
        for (auto& ts : j["touchscreen"]) {
            auto* screen = out->add_touchscreen();
            if (!ts.contains("width") || !ts.contains("height"))
                return "touchscreen requires width and height";
            screen->set_width(ts["width"].get<int32_t>());
            screen->set_height(ts["height"].get<int32_t>());
        }
    }
    if (j.contains("keycodes_supported")) {
        for (auto& kc : j["keycodes_supported"])
            out->add_keycodes_supported(kc.get<int32_t>());
    }
    return {};
}

std::string buildBluetooth(const json& j,
                           service::bluetooth::BluetoothService* out) {
    if (!j.contains("car_address"))
        return "bluetooth_service requires car_address";
    out->set_car_address(j["car_address"].get<std::string>());

    if (j.contains("supported_pairing_methods")) {
        for (auto& pm : j["supported_pairing_methods"]) {
            service::bluetooth::message::BluetoothPairingMethod v;
            auto name = pm.get<std::string>();
            if (!service::bluetooth::message::BluetoothPairingMethod_Parse(name, &v))
                return "invalid pairing method: '" + name + "'";
            out->add_supported_pairing_methods(v);
        }
    }
    return {};
}

std::string buildChannel(const json& j, service::Service* out) {
    if (!j.contains("id"))
        return "channel requires 'id'";
    out->set_id(j["id"].get<int32_t>());

    if (j.contains("media_sink_service"))
        return buildMediaSink(j["media_sink_service"],
                              out->mutable_media_sink_service());
    if (j.contains("media_source_service"))
        return buildMediaSource(j["media_source_service"],
                                out->mutable_media_source_service());
    if (j.contains("sensor_source_service"))
        return buildSensorSource(j["sensor_source_service"],
                                 out->mutable_sensor_source_service());
    if (j.contains("input_source_service"))
        return buildInputSource(j["input_source_service"],
                                out->mutable_input_source_service());
    if (j.contains("bluetooth_service"))
        return buildBluetooth(j["bluetooth_service"],
                              out->mutable_bluetooth_service());
    return {};
}

std::string buildPingConfig(
    const json& j,
    aap_protobuf::service::control::message::PingConfiguration* out) {
    if (j.contains("timeout_ms"))
        out->set_timeout_ms(j["timeout_ms"].get<uint32_t>());
    if (j.contains("interval_ms"))
        out->set_interval_ms(j["interval_ms"].get<uint32_t>());
    if (j.contains("high_latency_threshold_ms"))
        out->set_high_latency_threshold_ms(
            j["high_latency_threshold_ms"].get<uint32_t>());
    if (j.contains("tracked_ping_count"))
        out->set_tracked_ping_count(j["tracked_ping_count"].get<uint32_t>());
    return {};
}

std::string buildConnectionConfig(
    const json& j,
    aap_protobuf::service::control::message::ConnectionConfiguration* out) {
    if (j.contains("ping_configuration"))
        return buildPingConfig(j["ping_configuration"],
                               out->mutable_ping_configuration());
    return {};
}

std::string buildHeadUnitInfo(
    const json& j,
    aap_protobuf::service::control::message::HeadUnitInfo* out) {
    if (j.contains("make"))
        out->set_make(j["make"].get<std::string>());
    if (j.contains("model"))
        out->set_model(j["model"].get<std::string>());
    if (j.contains("year"))
        out->set_year(j["year"].get<std::string>());
    if (j.contains("vehicle_id"))
        out->set_vehicle_id(j["vehicle_id"].get<std::string>());
    if (j.contains("head_unit_make"))
        out->set_head_unit_make(j["head_unit_make"].get<std::string>());
    if (j.contains("head_unit_model"))
        out->set_head_unit_model(j["head_unit_model"].get<std::string>());
    if (j.contains("head_unit_software_build"))
        out->set_head_unit_software_build(
            j["head_unit_software_build"].get<std::string>());
    if (j.contains("head_unit_software_version"))
        out->set_head_unit_software_version(
            j["head_unit_software_version"].get<std::string>());
    return {};
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ServiceConfig public API
// ---------------------------------------------------------------------------

ServiceConfig::ServiceConfig(std::string defaultPath, std::string userPath)
    : defaultPath_(std::move(defaultPath)),
      userPath_(std::move(userPath)) {}

bool ServiceConfig::load() {
    std::lock_guard lk(mu_);

    // Load defaults
    std::ifstream defFile(defaultPath_);
    if (!defFile) {
        OPENAUTO_LOG(error) << "[ServiceConfig] cannot open default config: "
                            << defaultPath_;
        return false;
    }
    try {
        config_ = json::parse(defFile);
    } catch (const json::exception& e) {
        OPENAUTO_LOG(error) << "[ServiceConfig] default JSON parse error: "
                            << e.what();
        return false;
    }

    // Overlay user config if present
    std::ifstream userFile(userPath_);
    if (userFile) {
        try {
            config_ = json::parse(userFile);
            OPENAUTO_LOG(info) << "[ServiceConfig] loaded user config from "
                               << userPath_;
        } catch (const json::exception& e) {
            OPENAUTO_LOG(warning)
                << "[ServiceConfig] user JSON parse error, using defaults: "
                << e.what();
        }
    }

    // Validate by building proto
    SDR proto;
    auto err = buildProto(config_, proto);
    if (!err.empty()) {
        OPENAUTO_LOG(error) << "[ServiceConfig] config validation failed: "
                            << err;
        return false;
    }

    OPENAUTO_LOG(info) << "[ServiceConfig] config loaded successfully";
    return true;
}

bool ServiceConfig::save() {
    std::lock_guard lk(mu_);

    std::ofstream out(userPath_);
    if (!out) {
        OPENAUTO_LOG(error) << "[ServiceConfig] cannot write user config: "
                            << userPath_;
        return false;
    }
    out << config_.dump(2) << '\n';
    OPENAUTO_LOG(info) << "[ServiceConfig] saved user config to " << userPath_;
    return true;
}

bool ServiceConfig::reset() {
    std::lock_guard lk(mu_);

    // Remove user file
    std::remove(userPath_.c_str());

    // Reload from defaults
    std::ifstream defFile(defaultPath_);
    if (!defFile) {
        OPENAUTO_LOG(error) << "[ServiceConfig] cannot open default config: "
                            << defaultPath_;
        return false;
    }
    try {
        config_ = json::parse(defFile);
    } catch (const json::exception& e) {
        OPENAUTO_LOG(error) << "[ServiceConfig] default JSON parse error: "
                            << e.what();
        return false;
    }

    OPENAUTO_LOG(info) << "[ServiceConfig] reset to defaults";
    return true;
}

std::string ServiceConfig::getJson() const {
    std::lock_guard lk(mu_);
    return config_.dump(2);
}

std::string ServiceConfig::setJson(const std::string& jsonStr) {
    json candidate;
    try {
        candidate = json::parse(jsonStr);
    } catch (const json::exception& e) {
        return std::string("JSON parse error: ") + e.what();
    }

    // Validate by building proto
    SDR proto;
    auto err = buildProto(candidate, proto);
    if (!err.empty())
        return err;

    std::lock_guard lk(mu_);
    config_ = std::move(candidate);
    return {};
}

SDR ServiceConfig::toProto() const {
    std::lock_guard lk(mu_);
    SDR proto;
    auto err = buildProto(config_, proto);
    if (!err.empty())
        OPENAUTO_LOG(error) << "[ServiceConfig] toProto failed (should not "
                               "happen after validation): " << err;
    return proto;
}

std::string ServiceConfig::toTextProto() const {
    auto proto = toProto();
    std::string out;
    google::protobuf::TextFormat::PrintToString(proto, &out);
    return out;
}

// ---------------------------------------------------------------------------
// buildProto — the JSON-to-proto mapper / validator
// ---------------------------------------------------------------------------

std::string ServiceConfig::buildProto(const json& j, SDR& out) {
    try {
        // Channels
        if (j.contains("channels")) {
            if (!j["channels"].is_array())
                return "'channels' must be an array";
            for (auto& ch : j["channels"]) {
                auto err = buildChannel(ch, out.add_channels());
                if (!err.empty()) return err;
            }
        }

        // Scalars
        if (j.contains("display_name"))
            out.set_display_name(j["display_name"].get<std::string>());

        if (j.contains("driver_position")) {
            aap_protobuf::service::control::message::DriverPosition dp;
            auto err = PARSE_ENUM(
                j, "driver_position", &dp,
                aap_protobuf::service::control::message::DriverPosition);
            if (!err.empty()) return err;
            out.set_driver_position(dp);
        }

        if (j.contains("can_play_native_media_during_vr"))
            out.set_can_play_native_media_during_vr(
                j["can_play_native_media_during_vr"].get<bool>());

        if (j.contains("probe_for_support"))
            out.set_probe_for_support(j["probe_for_support"].get<bool>());

        if (j.contains("session_configuration"))
            out.set_session_configuration(
                j["session_configuration"].get<int32_t>());

        // Connection configuration
        if (j.contains("connection_configuration")) {
            auto err = buildConnectionConfig(
                j["connection_configuration"],
                out.mutable_connection_configuration());
            if (!err.empty()) return err;
        }

        // Head unit info
        if (j.contains("headunit_info")) {
            auto err = buildHeadUnitInfo(j["headunit_info"],
                                         out.mutable_headunit_info());
            if (!err.empty()) return err;
        }

    } catch (const json::exception& e) {
        return std::string("JSON type error: ") + e.what();
    }
    return {};
}

#undef PARSE_ENUM

} // namespace f1x::openauto::autoapp::configuration
