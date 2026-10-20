#include "offline/OfflinePipeline.h"

#include <algorithm>
#include <array>
#include <stdexcept>

#include "dsp/StreamingDsp.h"

namespace cleanwave {

std::vector<std::int16_t> ProcessOfflinePcm16(
    OfflineCleaner& cleaner,
    std::span<const std::int16_t> interleaved_stereo, bool run_model) {
  if ((interleaved_stereo.size() % 2) != 0) {
    throw std::runtime_error("offline PCM must contain complete stereo frames");
  }
  const std::size_t total_frames = interleaved_stereo.size() / 2;
  // A finite file is an independent stream, including an empty file. Preserve
  // user controls while resetting model/history/synthesis and AudioLevel state.
  cleaner.ResetAll();
  std::vector<std::int16_t> result(total_frames * 2);
  if (total_frames == 0) return result;

  std::size_t input_cursor_frames = 0;
  std::size_t output_cursor_frames = 0;
  bool first = true;
  OfflineCleaner::PcmHop hop_output{};

  while (output_cursor_frames < total_frames) {
    const std::size_t required_frames =
        first ? StreamingDsp::kFirstInputFrames
              : StreamingDsp::kHopFrames;
    std::vector<std::int16_t> packet(required_frames * 2, 0);
    const std::size_t available_frames =
        input_cursor_frames < total_frames
            ? std::min(required_frames, total_frames - input_cursor_frames)
            : 0;
    if (available_frames != 0) {
      const auto source_begin =
          interleaved_stereo.begin() + static_cast<std::ptrdiff_t>(input_cursor_frames * 2);
      std::copy_n(source_begin, available_frames * 2, packet.begin());
      input_cursor_frames += available_frames;
    }

    cleaner.ProcessPcm16(packet, first, hop_output, false, run_model);
    const std::size_t write_frames =
        std::min(StreamingDsp::kHopFrames,
                 total_frames - output_cursor_frames);
    std::copy_n(hop_output.begin(), write_frames * 2,
                result.begin() +
                    static_cast<std::ptrdiff_t>(output_cursor_frames * 2));
    output_cursor_frames += write_frames;
    first = false;
  }
  return result;
}

}  // namespace cleanwave
