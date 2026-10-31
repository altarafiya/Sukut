#pragma once

// The Windows pipeline's input and output contracts (StreamInput / StreamOutput),
// served by PipeWire through the bridge's rings instead of WASAPI clients.
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/PipeWireBridge.h"
#include "dsp/StreamingDsp.h"
#include "realtime/BufferedAudioPipeline.h"

namespace sukut::linux_audio {

// Optional record of exactly what the engine received and produced, for the
// stage 3 parity test (offline replay must give the same output).
struct EngineTap {
  std::vector<std::int16_t> input, output;
  std::vector<std::uint64_t> discontinuities;  // input frame positions
  bool enabled = false;
};

class PipeWireInput final : public cleanwave::StreamInput {
 public:
  PipeWireInput(PipeWireBridge& bridge, EngineTap* tap) : bridge_(bridge), tap_(tap) {}
  // Sukut's sink runs continuously while linked; idle reads only mean the
  // engine is ahead of PipeWire's cycle.
  bool AllowsIdleCapture() const override { return true; }
  void Open(std::stop_token) override;
  cleanwave::ReadResult Read(std::span<std::int16_t> destination, std::stop_token stop) override;

 private:
  PipeWireBridge& bridge_;
  EngineTap* tap_;
  std::uint64_t delivered_ = 0;
};

class PipeWireOutput final : public cleanwave::StreamOutput {
 public:
  PipeWireOutput(PipeWireBridge& bridge, std::shared_ptr<std::atomic<int>> amplification_db, EngineTap* tap)
      : bridge_(bridge), amplification_db_(std::move(amplification_db)), tap_(tap) {}
  std::size_t Write(std::span<const std::int16_t> source, std::uint64_t first_frame, std::stop_token stop) override;

 private:
  PipeWireBridge& bridge_;
  std::shared_ptr<std::atomic<int>> amplification_db_;
  EngineTap* tap_;
  std::uint64_t next_frame_ = 0;
  float gain_ = 1.0F;
  std::array<std::int16_t, cleanwave::StreamingDsp::kHopFrames * 2> scratch_{};
};

}  // namespace sukut::linux_audio
