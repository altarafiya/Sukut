#include "dsp/AudioLevel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cleanwave {

void AudioLevel::SetReductionPercent(int percent) {
  reduction_ = static_cast<float>(std::clamp(percent, 0, 100)) / 100.0F;
}

void AudioLevel::SetVoiceBoostDb(int db) {
  boost_db_ = std::clamp(db, 0, 6);
  boost_gain_ = static_cast<float>(std::pow(10.0, boost_db_ / 20.0));
}

void AudioLevel::ResetAmount() { amount_ = reduction_; }

void AudioLevel::ResetStreamState() {
  amount_ = reduction_;
  gain_ = 1.0F;
}

void AudioLevel::Render(const StreamingDsp::HopStereo& dry,
                        const StreamingDsp::HopStereo& wet,
                        PcmHop& output, bool paused) {
  double dry_power = 0.0;
  double wet_power = 0.0;
  for (std::size_t i = 0; i < dry.size(); ++i) {
    if (!std::isfinite(wet[i])) {
      throw std::runtime_error("model returned a non-finite sample");
    }
    const float dry_square = dry[i] * dry[i];
    const float wet_square = wet[i] * wet[i];
    dry_power += dry_square;
    wet_power += wet_square;
  }

  const double ratio = std::sqrt(wet_power / (dry_power + 1.0e-20));
  float compensation = 1.0F;
  if (ratio >= 0.32 && wet_power / static_cast<double>(wet.size()) > 1.0e-7) {
    compensation =
        static_cast<float>(std::clamp(1.0 / ratio, 1.0, 1.5));
  }
  const float next_gain = gain_ + 0.15F * (compensation - gain_);
  const float target = paused ? 0.0F : reduction_;

  for (std::size_t frame = 0; frame < StreamingDsp::kHopFrames; ++frame) {
    amount_ += std::clamp(target - amount_, -0.004F, 0.004F);
    const float level =
        (gain_ + (next_gain - gain_) * static_cast<float>(frame) /
                     static_cast<float>(StreamingDsp::kHopFrames)) *
        boost_gain_;
    for (std::size_t channel = 0; channel < 2; ++channel) {
      const std::size_t i = frame * 2 + channel;
      float sample = dry[i] * (1.0F - amount_) + wet[i] * level * amount_;
      if (std::abs(sample) > 0.9F && amount_ > 0.0F) {
        const float sign = sample < 0.0F ? -1.0F : 1.0F;
        sample = sign *
                 (0.9F + 0.099F *
                              std::tanh((std::abs(sample) - 0.9F) / 0.099F));
      }
      sample = std::clamp(sample, -1.0F, 32767.0F / 32768.0F);
      const int pcm = static_cast<int>(sample * 32768.0F);
      output[i] = static_cast<std::int16_t>(pcm);
    }
  }
  gain_ = next_gain;
}

void DecodeStereoPcm16(std::span<const std::int16_t> pcm,
                       std::span<float> destination) {
  if (destination.size() < pcm.size()) {
    throw std::runtime_error("PCM decode destination is too small");
  }
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    destination[i] = static_cast<float>(pcm[i]) / 32768.0F;
  }
}

}  // namespace cleanwave
