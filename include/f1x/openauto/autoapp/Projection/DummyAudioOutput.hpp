#pragma once
#include <cstdint>
#include <boost/asio/io_context.hpp>
#include <f1x/openauto/autoapp/Projection/IAudioOutput.hpp>
#include <aasdk/Common/Data.hpp>
#include <aasdk/Messenger/Timestamp.hpp>

namespace f1x::openauto::autoapp::projection {

class DummyAudioOutput final : public IAudioOutput {
public:
  DummyAudioOutput(boost::asio::io_context& /*io_context*/,
                   uint32_t channelCount,
                   uint32_t sampleSize,
                   uint32_t sampleRate)
      : channelCount_(channelCount), sampleSize_(sampleSize), sampleRate_(sampleRate) {}

  bool open() override { return true; }
  void write(aasdk::messenger::Timestamp::ValueType /*ts*/,
             const aasdk::common::DataConstBuffer& /*buf*/) override {}
  void start() override {}
  void stop() override {}
  void suspend() override {}

  uint32_t getSampleSize() const override { return sampleSize_; }
  uint32_t getChannelCount() const override { return channelCount_; }
  uint32_t getSampleRate() const override { return sampleRate_; }

private:
  uint32_t channelCount_;
  uint32_t sampleSize_;
  uint32_t sampleRate_;
};

} // namespace f1x::openauto::autoapp::projection