#include "audio/PipeWireAdapters.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace sukut::linux_audio {

void PipeWireInput::Open(std::stop_token) {
  // Audio captured before the engine was ready (warmup) is not part of the
  // stream: start from the newest frame, with a fresh model segment.
  FrameRing& ring = bridge_.CaptureRing();
  ring.DropAll();
  bridge_.ClearGap(bridge_.GapAt());
  delivered_ = 0;
}

cleanwave::ReadResult PipeWireInput::Read(std::span<std::int16_t> destination, std::stop_token stop) {
  cleanwave::ReadResult result;
  if (stop.stop_requested()) return result;
  FrameRing& ring = bridge_.CaptureRing();
  const std::uint64_t position = ring.ReadIndex();
  std::size_t wanted = destination.size() / 2;
  bool discontinuity = false;
  const std::uint64_t gap = bridge_.GapAt();
  if (gap != kNoGap) {
    if (gap <= position) discontinuity = true;  // frames from here follow lost ones
    else if (gap < position + wanted) wanted = static_cast<std::size_t>(gap - position);  // stop at the gap
  }
  result.frames = ring.Read(destination.first(wanted * 2));
  if (result.frames == 0) return result;  // a discontinuity waits until data follows it
  if (discontinuity) {
    bridge_.ClearGap(gap);
    result.discontinuity = true;
    if (tap_ != nullptr && tap_->enabled) tap_->discontinuities.push_back(delivered_);
  }
  if (tap_ != nullptr && tap_->enabled)
    tap_->input.insert(tap_->input.end(), destination.begin(), destination.begin() + static_cast<std::ptrdiff_t>(result.frames * 2));
  delivered_ += result.frames;
  return result;
}

std::size_t PipeWireOutput::Write(std::span<const std::int16_t> source, std::uint64_t first_frame, std::stop_token stop) {
  if (stop.stop_requested()) return 0;
  if (source.empty() || source.size() % 2 != 0 || source.size() > scratch_.size()) throw std::logic_error("invalid render write");
  if (first_frame != next_frame_) throw std::runtime_error("render timeline mismatch");
  FrameRing& ring = bridge_.PlaybackRing();
  const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(source.size() / 2, ring.Capacity() - ring.Fill()));
  if (frames == 0) return 0;  // the pipeline waits and retries
  const auto pcm = source.first(static_cast<std::size_t>(frames) * 2);

  // Amplification exactly as the Windows output (WasapiRender::Write): 0 dB is
  // an unchanged copy; a boost is limited to this chunk's headroom and smoothed.
  const int boost_db = amplification_db_ ? std::clamp(amplification_db_->load(std::memory_order_relaxed), 0, 12) : 0;
  if (boost_db == 0 && gain_ <= 1.00001F) {
    gain_ = 1.0F;
    std::copy(pcm.begin(), pcm.end(), scratch_.begin());
  } else {
    const float requested = std::pow(10.0F, static_cast<float>(boost_db) / 20.0F);
    int peak = 0;
    for (const auto sample : pcm) peak = std::max(peak, std::abs(static_cast<int>(sample)));
    const float safe = peak ? std::max(1.0F, 32767.0F / static_cast<float>(peak)) : requested;
    const float target = std::min(requested, safe);
    gain_ = std::min(gain_, safe);
    const float smoothing = 1.0F / static_cast<float>(kRate / 200);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
      gain_ += (target - gain_) * smoothing;
      for (std::uint32_t channel = 0; channel < 2; ++channel) {
        const float value = std::round(static_cast<float>(pcm[frame * 2 + channel]) * gain_);
        scratch_[frame * 2 + channel] = static_cast<std::int16_t>(std::clamp(value, -32768.0F, 32767.0F));
      }
    }
  }
  const auto kept = ring.Write(std::span<const std::int16_t>(scratch_.data(), static_cast<std::size_t>(frames) * 2));
  if (tap_ != nullptr && tap_->enabled)
    tap_->output.insert(tap_->output.end(), scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(kept * 2));
  next_frame_ += kept;
  return kept;
}

}  // namespace sukut::linux_audio
