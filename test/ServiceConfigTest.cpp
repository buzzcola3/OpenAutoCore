#include <gtest/gtest.h>

#include <Configuration/ServiceConfig.hpp>
#include <Configuration/ProtoConfig.hpp>

#include <google/protobuf/text_format.h>
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>

#include <cstdlib>
#include <fstream>
#include <string>

using SDR = aap_protobuf::service::control::message::ServiceDiscoveryResponse;
using ServiceConfig = f1x::openauto::autoapp::configuration::ServiceConfig;

namespace {

// Resolve a runfiles path relative to the workspace root.
std::string runfile(const std::string& rel) {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace)
        return std::string(srcdir) + "/" + workspace + "/" + rel;
    // Fallback: assume CWD is workspace root.
    return rel;
}

std::string toTextProto(const SDR& msg) {
    std::string out;
    google::protobuf::TextFormat::PrintToString(msg, &out);
    return out;
}

const std::string kDefaultJson =
    "configuration/ServiceDiscoveryResponse.default.json";
const std::string kTextProto =
    "configuration/ServiceDiscoveryResponse.textproto";

} // namespace

// ---------------------------------------------------------------------------
// Test: JSON-generated proto matches the existing textproto exactly
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, GeneratedProtoMatchesTextproto) {
    // 1. Load the existing textproto into a proto.
    SDR textprotoProto;
    ASSERT_TRUE(f1x::openauto::autoapp::config::loadTextProto(
        runfile(kTextProto), textprotoProto, "textproto"));

    // 2. Load JSON via ServiceConfig and generate proto.
    // Use a non-existent user path so only defaults are loaded.
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_nonexistent_user.json");
    ASSERT_TRUE(cfg.load());
    SDR jsonProto = cfg.toProto();

    // 3. Compare proto-to-proto via serialized bytes.
    EXPECT_EQ(textprotoProto.SerializeAsString(), jsonProto.SerializeAsString())
        << "Textproto proto:\n" << toTextProto(textprotoProto)
        << "\nJSON proto:\n" << toTextProto(jsonProto);
}

// ---------------------------------------------------------------------------
// Test: toTextProto round-trips back to the same proto
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, TextProtoRoundTrip) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_nonexistent_user.json");
    ASSERT_TRUE(cfg.load());

    // Generate textproto string, re-parse it, compare to direct toProto().
    std::string textproto = cfg.toTextProto();
    ASSERT_FALSE(textproto.empty());

    SDR reparsed;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(textproto, &reparsed));

    SDR direct = cfg.toProto();

    EXPECT_EQ(direct.SerializeAsString(), reparsed.SerializeAsString())
        << "Direct:\n" << toTextProto(direct)
        << "\nReparsed:\n" << toTextProto(reparsed);
}

// ---------------------------------------------------------------------------
// Test: getJson returns valid parseable JSON
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, GetJsonReturnsParseable) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_nonexistent_user.json");
    ASSERT_TRUE(cfg.load());

    std::string jsonStr = cfg.getJson();
    ASSERT_FALSE(jsonStr.empty());

    auto parsed = nlohmann::json::parse(jsonStr, nullptr, false);
    EXPECT_FALSE(parsed.is_discarded()) << "getJson() returned invalid JSON";
}

// ---------------------------------------------------------------------------
// Test: setJson with valid JSON succeeds
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, SetJsonValid) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_set_valid.json");
    ASSERT_TRUE(cfg.load());

    // Get current config, modify display_name, set it back.
    auto j = nlohmann::json::parse(cfg.getJson());
    j["display_name"] = "TestUnit";
    std::string err = cfg.setJson(j.dump());
    EXPECT_TRUE(err.empty()) << "setJson error: " << err;

    // Verify the change is reflected.
    auto j2 = nlohmann::json::parse(cfg.getJson());
    EXPECT_EQ(j2["display_name"], "TestUnit");

    // Verify proto reflects the change.
    SDR proto = cfg.toProto();
    EXPECT_EQ(proto.display_name(), "TestUnit");

    // Cleanup
    std::remove("/tmp/service_config_test_set_valid.json");
}

// ---------------------------------------------------------------------------
// Test: setJson with invalid enum value is rejected
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, SetJsonInvalidEnumRejected) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_invalid.json");
    ASSERT_TRUE(cfg.load());

    auto j = nlohmann::json::parse(cfg.getJson());
    j["driver_position"] = "INVALID_POSITION";
    std::string err = cfg.setJson(j.dump());
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("INVALID_POSITION"), std::string::npos);

    // Verify the original config is unchanged.
    auto j2 = nlohmann::json::parse(cfg.getJson());
    EXPECT_EQ(j2["driver_position"], "DRIVER_POSITION_RIGHT");
}

// ---------------------------------------------------------------------------
// Test: setJson with malformed JSON is rejected
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, SetJsonMalformedRejected) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_malformed.json");
    ASSERT_TRUE(cfg.load());

    std::string err = cfg.setJson("{not valid json");
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("parse"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: reset restores defaults after setJson
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, ResetRestoresDefaults) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_reset.json");
    ASSERT_TRUE(cfg.load());

    // Modify config.
    auto j = nlohmann::json::parse(cfg.getJson());
    std::string originalName = j["display_name"];
    j["display_name"] = "Modified";
    ASSERT_TRUE(cfg.setJson(j.dump()).empty());
    ASSERT_TRUE(cfg.save());

    // Reset and verify defaults are restored.
    ASSERT_TRUE(cfg.reset());
    auto j2 = nlohmann::json::parse(cfg.getJson());
    EXPECT_EQ(j2["display_name"], originalName);

    // Cleanup
    std::remove("/tmp/service_config_test_reset.json");
}

// ---------------------------------------------------------------------------
// Test: save and reload persists changes
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, SaveAndReload) {
    const std::string userPath = "/tmp/service_config_test_persist.json";
    std::remove(userPath.c_str());

    {
        ServiceConfig cfg(runfile(kDefaultJson), userPath);
        ASSERT_TRUE(cfg.load());

        auto j = nlohmann::json::parse(cfg.getJson());
        j["display_name"] = "Persisted";
        ASSERT_TRUE(cfg.setJson(j.dump()).empty());
        ASSERT_TRUE(cfg.save());
    }

    // New instance should pick up the saved user config.
    {
        ServiceConfig cfg2(runfile(kDefaultJson), userPath);
        ASSERT_TRUE(cfg2.load());

        auto j2 = nlohmann::json::parse(cfg2.getJson());
        EXPECT_EQ(j2["display_name"], "Persisted");
    }

    std::remove(userPath.c_str());
}

// ---------------------------------------------------------------------------
// Test: load fails with missing default file
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, LoadFailsMissingDefault) {
    ServiceConfig cfg("/tmp/nonexistent_default_12345.json",
                      "/tmp/nonexistent_user_12345.json");
    EXPECT_FALSE(cfg.load());
}

// ---------------------------------------------------------------------------
// Test: setJson with invalid channel field is rejected
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, SetJsonInvalidChannelEnumRejected) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_invalid_ch.json");
    ASSERT_TRUE(cfg.load());

    auto j = nlohmann::json::parse(cfg.getJson());
    // Corrupt the first channel's video config resolution
    j["channels"][0]["media_sink_service"]["video_configs"][0]["codec_resolution"] =
        "VIDEO_INVALID_RES";
    std::string err = cfg.setJson(j.dump());
    EXPECT_FALSE(err.empty());
    EXPECT_NE(err.find("VIDEO_INVALID_RES"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: all channels from default config produce valid protos
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, DefaultConfigAllChannelsValid) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_nonexistent_user.json");
    ASSERT_TRUE(cfg.load());

    SDR proto = cfg.toProto();
    EXPECT_EQ(proto.channels_size(), 8);

    // Verify each channel has an id set.
    for (int i = 0; i < proto.channels_size(); ++i) {
        EXPECT_TRUE(proto.channels(i).has_id())
            << "Channel at index " << i << " missing id";
    }
}

// ---------------------------------------------------------------------------
// Test: proto field values match expected defaults
// ---------------------------------------------------------------------------
TEST(ServiceConfigTest, DefaultProtoFieldValues) {
    ServiceConfig cfg(runfile(kDefaultJson),
                      "/tmp/service_config_test_nonexistent_user.json");
    ASSERT_TRUE(cfg.load());

    SDR proto = cfg.toProto();
    EXPECT_EQ(proto.display_name(), "OpenAutoCore");
    EXPECT_EQ(proto.driver_position(),
              aap_protobuf::service::control::message::DRIVER_POSITION_RIGHT);
    EXPECT_FALSE(proto.can_play_native_media_during_vr());
    EXPECT_FALSE(proto.probe_for_support());

    // Connection config
    ASSERT_TRUE(proto.has_connection_configuration());
    ASSERT_TRUE(proto.connection_configuration().has_ping_configuration());
    auto& ping = proto.connection_configuration().ping_configuration();
    EXPECT_EQ(ping.timeout_ms(), 3000u);
    EXPECT_EQ(ping.interval_ms(), 1000u);
    EXPECT_EQ(ping.high_latency_threshold_ms(), 200u);
    EXPECT_EQ(ping.tracked_ping_count(), 5u);

    // Headunit info
    ASSERT_TRUE(proto.has_headunit_info());
    EXPECT_EQ(proto.headunit_info().make(), "blank");
    EXPECT_EQ(proto.headunit_info().head_unit_software_version(), "1.0");
}
