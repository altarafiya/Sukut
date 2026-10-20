#include "dsp/OfflineCleaner.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cleanwave {

OfflineCleaner::OfflineCleaner(
    const std::filesystem::path& model_path,
    const std::filesystem::path& rise_window_path,
    const std::filesystem::path& hop_window_path)
    : dsp_(model_path, rise_window_path, hop_window_path) {}

void OfflineCleaner::ProcessPcm16(
    std::span<const std::int16_t> interleaved_stereo, bool first,
    PcmHop& output, bool paused, bool run_model) {
  const std::size_t expected_samples =
      (first ? StreamingDsp::kFirstInputFrames
             : StreamingDsp::kHopFrames) *
      2;
  if (interleaved_stereo.size() != expected_samples) {
    throw std::runtime_error("unexpected PCM packet size");
  }

  if (first) {
    DecodeStereoPcm16(interleaved_stereo, first_decoded_);
    dsp_.Process(first_decoded_, true, run_model, voice_q12_, music_q12_,
                 other_q12_, dry_, wet_);
    level_.ResetAmount();
  } else {
    DecodeStereoPcm16(interleaved_stereo, hop_decoded_);
    dsp_.Process(hop_decoded_, false, run_model, voice_q12_, music_q12_,
                 other_q12_, dry_, wet_);
  }
  level_.Render(dry_, wet_, output, paused);
}

void OfflineCleaner::SetReductionPercent(int percent) {
  level_.SetReductionPercent(percent);
}

void OfflineCleaner::SetVoiceBoostDb(int db) {
  level_.SetVoiceBoostDb(db);
}

void OfflineCleaner::SetSourceMixDb(int voice_db, int music_db,
                                           int other_db) {
  voice_q12_ = DbToQ12(voice_db);
  music_q12_ = DbToQ12(music_db);
  other_q12_ = DbToQ12(other_db);
}

void OfflineCleaner::ResetNeuralState() { dsp_.ResetNeuralState(); }
void OfflineCleaner::ResetAll() {
  dsp_.ResetAll();
  level_.ResetStreamState();
}

SeparationModel::StateBuffer OfflineCleaner::ModelStateSnapshot() const {
  return dsp_.ModelStateSnapshot();
}

int OfflineCleaner::DbToQ12(int db) {
  const int clamped = std::clamp(db, kSourceMixMinDb, kSourceMixMaxDb);
  return static_cast<int>(
      std::lround(4096.0 * std::pow(10.0, static_cast<double>(clamped) / 20.0)));
}

}  // namespace cleanwave
