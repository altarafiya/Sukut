#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "dsp/OfflineCleaner.h"
#include "offline/OfflinePipeline.h"
#include "offline/WavIO.h"

namespace {

struct Options {
  std::filesystem::path input;
  std::filesystem::path output;
  std::filesystem::path model = "assets/engine/separator.tflite";
  std::filesystem::path rise = "assets/engine/window_576_q15.bin";
  std::filesystem::path hop = "assets/engine/window_960_q15.bin";
  int voice_db = 0;
  int music_db = -10;
  int other_db = -20;
  int reduction = 100;
  int boost_db = 0;
};

int ParseInt(const std::wstring& value, const char* name) {
  std::size_t used = 0;
  const int result = std::stoi(value, &used);
  if (used != value.size()) throw std::runtime_error(std::string("invalid ") + name);
  return result;
}

Options Parse(int argc, wchar_t** argv) {
  if (argc < 3) {
    throw std::runtime_error(
        "usage: cleanwave_offline <input.wav> <output.wav> "
        "[--voice-db N --music-db N --other-db N --reduction N --boost-db N "
        "--model PATH --rise-window PATH --hop-window PATH]");
  }
  Options options;
  options.input = argv[1];
  options.output = argv[2];
  for (int i = 3; i < argc; ++i) {
    const std::wstring key = argv[i];
    if (i + 1 >= argc) throw std::runtime_error("missing option value");
    const std::wstring value = argv[++i];
    if (key == L"--voice-db") options.voice_db = ParseInt(value, "voice dB");
    else if (key == L"--music-db") options.music_db = ParseInt(value, "music dB");
    else if (key == L"--other-db") options.other_db = ParseInt(value, "other dB");
    else if (key == L"--reduction") options.reduction = ParseInt(value, "reduction");
    else if (key == L"--boost-db") options.boost_db = ParseInt(value, "boost dB");
    else if (key == L"--model") options.model = value;
    else if (key == L"--rise-window") options.rise = value;
    else if (key == L"--hop-window") options.hop = value;
    else throw std::runtime_error("unknown option");
  }
  return options;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  try {
    const Options options = Parse(argc, argv);
    auto input = cleanwave::ReadPcm16StereoWav(options.input);
    cleanwave::OfflineCleaner cleaner(options.model, options.rise, options.hop);
    cleaner.SetSourceMixDb(options.voice_db, options.music_db, options.other_db);
    cleaner.SetReductionPercent(options.reduction);
    cleaner.SetVoiceBoostDb(options.boost_db);
    const auto output_samples =
        cleanwave::ProcessOfflinePcm16(cleaner, input.samples, true);

    cleanwave::WavPcm16Stereo output;
    output.sample_rate = input.sample_rate;
    output.samples = output_samples;
    cleanwave::WritePcm16StereoWav(options.output, output);

    std::cout << "CleanWave Step 4 offline WAV processing\n"
              << "input_frames=" << input.FrameCount() << "\n"
              << "output_frames=" << output.FrameCount() << "\n"
              << "format=PCM16 stereo 48000 Hz\n"
              << "fft=1536 hop=960 overlap=576\n"
              << "source_q12="
              << cleanwave::OfflineCleaner::DbToQ12(options.voice_db) << ','
              << cleanwave::OfflineCleaner::DbToQ12(options.music_db) << ','
              << cleanwave::OfflineCleaner::DbToQ12(options.other_db) << "\n"
              << "finite_file_policy=zero_future_context_trim_exact_duration\n"
              << "RESULT=PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "CleanWave offline processing failed: " << error.what() << '\n';
    return 1;
  }
}
